#include "WorldRenderer.hpp"
#include "ArtScaler.hpp"
#include "RenderSettings.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

// ============================================================================
// WORLD RENDERER IMPLEMENTATION
// ============================================================================
// Implements isometric rendering following original ZT1 specification
// Reads WorldMap data, queries SpriteDatabase for textures
//
// The map is a heightfield with cliffs:
//   - Every tile has four corner heights (see WorldMap). Hills and pits are
//     tiles whose corners meet their neighbours exactly.
//   - Where a tile's front edge is higher than the neighbour in front of it,
//     a cliff face is drawn down to the neighbour's edge. The original draws
//     nothing past the map border, so neither do we.
//
// Shading is per vertex (Gouraud) from vertex normals, with brightness
// fitted to screenshots of the original game. Terrain types
// blend: every grid vertex mixes the terrains of the tiles around it, so a
// transition fades across one tile on each side of the edge. Types with
// blend=0 in tiletex.cfg (concrete, asphalt) stay hard-edged.
//
// Rendering runs in VIEW grid coordinates (u down-right, v down-left on
// screen); the view rotation maps them to world tiles. Rotation 0 is the
// game's default view, checked against the original game's minimap.
//
// Draw order is diagonal-by-diagonal (u + v ascending). Tiles on the same
// diagonal occupy disjoint screen columns, so the painter's algorithm is
// exact for a heightfield without per-tile sorting, and triangles within a
// diagonal can be grouped by texture.
// ============================================================================

namespace {

const int NUM_TERRAIN_TYPES = 18;

// Debug palette, indexed by terrain type
const SDL_Color kDebugColors[NUM_TERRAIN_TYPES] = {
    {0, 255, 0, 255},     // 0  Grass: Green
    {255, 255, 0, 255},   // 1  Savannah: Yellow
    {210, 180, 140, 255}, // 2  Sand: Tan
    {139, 69, 19, 255},   // 3  Dirt: Brown
    {0, 100, 0, 255},     // 4  Rainforest: Dark Green
    {165, 42, 42, 255},   // 5  Brown Stone: Red-Brown
    {128, 128, 128, 255}, // 6  Gray Stone: Gray
    {192, 192, 192, 255}, // 7  Gravel: Light Gray
    {255, 255, 255, 255}, // 8  Snow: White
    {0, 191, 255, 255},   // 9  Fresh Water: Sky Blue
    {0, 0, 128, 255},     // 10 Salt Water: Navy
    {107, 142, 35, 255},  // 11 Deciduous: Olive
    {0, 255, 255, 255},   // 12 Waterfall: Cyan
    {47, 79, 79, 255},    // 13 Conifer: Dark Slate
    {169, 169, 169, 255}, // 14 Concrete: Dark Gray
    {50, 50, 50, 255},    // 15 Asphalt: Charcoal
    {160, 130, 90, 255},  // 16 Trampled: Worn brown
    {120, 170, 190, 255}, // 17 Gunnite: Tank blue-gray
};

// Fallback palette for textured mode when a ground texture is missing
const SDL_Color kFallbackColors[NUM_TERRAIN_TYPES] = {
    {76, 175, 80, 255},   // 0  Grass
    {139, 195, 74, 255},  // 1  Savannah: Yellow-green
    {210, 180, 140, 255}, // 2  Sand: Tan
    {139, 90, 43, 255},   // 3  Dirt: Brown
    {46, 125, 50, 255},   // 4  Rainforest: Dark green
    {121, 85, 72, 255},   // 5  Brown Rock
    {96, 125, 139, 255},  // 6  Gray Rock
    {158, 158, 158, 255}, // 7  Gravel
    {240, 240, 240, 255}, // 8  Snow: White
    {33, 150, 243, 255},  // 9  Fresh Water: Blue
    {21, 101, 192, 255},  // 10 Salt Water: Dark blue
    {104, 159, 56, 255},  // 11 Deciduous
    {66, 165, 245, 255},  // 12 Waterfall
    {51, 105, 30, 255},   // 13 Coniferous
    {189, 189, 189, 255}, // 14 Concrete
    {66, 66, 66, 255},    // 15 Asphalt
    {160, 130, 90, 255},  // 16 Trampled
    {120, 170, 190, 255}, // 17 Gunnite
};

const SDL_Color kUnknownColor = {255, 0, 255, 255};

// Grid line tones. The shapes come from tiles.ztd's grid bitmaps (n*.bmp
// north edge, e*.bmp east edge: a 2 px line, upper and lower tone). The
// colours are as the original shows them on screen, averaged over land and
// water on Death Mountain (its display shifts the bitmaps' (84,59,49),
// (163,115,95) and (113,79,66) darker and more orange)
const SDL_Color kGridNorthUpper = {60, 27, 0, 255};
const SDL_Color kGridNorthLower = {108, 75, 48, 255};
const SDL_Color kGridEastUpper = {170, 120, 82, 255};
const SDL_Color kGridEastLower = {84, 50, 20, 255};

// Ground shading, fitted to the original game: screenshots of 16 maps were
// divided by our unlit render and regressed against our per-pixel world
// normals (13.6M pixels). The original's terrain/tilevar.cfg names two D3D
// lights, but taken literally they darken shadowed slopes far more than the
// game does; this fit matches it better (rms 0.045 vs 0.057).
const float kGroundLight = 0.964f;
const float kGroundLightX = -0.054f; // per unit of world normal x
const float kGroundLightY = 0.118f;  // per unit of world normal y

// Cliff faces, measured the same way (relative to the concrete texture):
// the face toward the lower right of the screen and toward the lower left
const float kRightCliffLight = 0.714f;
const float kLeftCliffLight = 0.435f;

// One height unit in tile-edge lengths. With 64x32 tiles a unit is 16 px on
// screen; for a 2:1 projection that is 16 / (0.866 * 64 / sqrt 2) = 0.408.
const float kHeightUnitInTiles = 0.408f;

// Ground textures: a 128 px texture repeats every 3 tiles in the original
// (measured from the repeat period of sand in a default-zoom screenshot)
const float kTexelsPerTile = 128.0f / 3.0f;

// Alternate tiles are slightly darker in debug mode so the grid stays
// readable without per-tile outlines
const float kCheckerShade = 0.92f;

SDL_Color terrainColor(const SDL_Color *palette, int type) {
  return (type >= 0 && type < NUM_TERRAIN_TYPES) ? palette[type]
                                                  : kUnknownColor;
}

SDL_Color shade(SDL_Color c, float k) {
  auto ch = [k](Uint8 v) {
    return static_cast<Uint8>(std::clamp(v * k, 0.0f, 255.0f));
  };
  return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

// Ground brightness for a world-space normal (x, y horizontal, z up)
float groundLight(float nx, float ny, float nz) {
  float len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len <= 0.0f)
    return kGroundLight;
  return std::clamp(kGroundLight + (kGroundLightX * nx + kGroundLightY * ny) / len,
                    0.5f, 1.2f);
}

// Unit world normal as a colour, for the normal-map debug view
SDL_Color encodeNormal(float x, float y, float z) {
  float len = std::sqrt(x * x + y * y + z * z);
  if (len <= 0)
    return {128, 128, 255, 255};
  auto ch = [](float v) {
    return static_cast<Uint8>(std::clamp(128.0f + 127.0f * v, 0.0f, 255.0f));
  };
  return {ch(x / len), ch(y / len), static_cast<Uint8>(255.0f * std::max(0.0f, z / len)), 255};
}

} // namespace

WorldRenderer::WorldRenderer()
    : tileWidth(64), tileHeight(32), elevationScale(16),
      elevationEnabled(true),
      debugTerrainIds(false) {}  // 0 key toggles flat debug colours

WorldRenderer::~WorldRenderer() {}

void WorldRenderer::setCamera(const Camera &cam) { camera = cam; }

void WorldRenderer::setTileSize(int width, int height) {
  tileWidth = width;
  tileHeight = height;
  SDL_Log("[WorldRenderer] Tile size set to %dx%d", width, height);
}

float WorldRenderer::getHeightUnitPixels() const {
  return elevationEnabled ? elevationScale * (tileHeight / 32.0f) : 0.0f;
}

// ============================================================================
// ISOMETRIC COORDINATE CONVERSION
// ============================================================================
// In VIEW grid coordinates (u down-right, v down-left on screen):
//   screenX = (u - v) * (tileWidth / 2)
//   screenY = (u + v) * (tileHeight / 2) - height * unitPixels
//
// The renderer draws under SDL_RenderSetScale(zoom), so every position here
// is in logical pixels. Dividing the screen center by zoom keeps the middle
// of the window fixed while zooming.
// ============================================================================

void WorldRenderer::rotateView(int steps) {
  viewRotation = ((viewRotation + steps) % 4 + 4) % 4;
  SDL_Log("[WorldRenderer] View rotation %d", viewRotation);
}

// The four rotations of the map, in 90 degree steps (W, H = map size):
//   0: world (W - pv, pu)      default view, verified against the original
//   1: world (W - pu, H - pv)
//   2: world (pv, H - pu)
//   3: world (pu, pv)
void WorldRenderer::viewToWorldVertex(int pu, int pv, int &x, int &y) const {
  const int W = mapWidthCache, H = mapHeightCache;
  switch (viewRotation) {
  case 0: x = W - pv; y = pu; break;
  case 1: x = W - pu; y = H - pv; break;
  case 2: x = pv; y = H - pu; break;
  default: x = pu; y = pv; break;
  }
}

void WorldRenderer::worldToViewVertex(int x, int y, int &pu, int &pv) const {
  const int W = mapWidthCache, H = mapHeightCache;
  switch (viewRotation) {
  case 0: pu = y; pv = W - x; break;
  case 1: pu = W - x; pv = H - y; break;
  case 2: pu = H - y; pv = x; break;
  default: pu = x; pv = y; break;
  }
}

// The original opens every map with the start camera's nearest edge at the
// lower left of the screen. Inferred from in-game screenshots: maps starting
// near x = 0 match rotation 0 (22 of 30 checked), under.zoo (near y = 0)
// matches rotation 1 (alignment 0.48 -> 0.70) and airport.zoo (near the far
// y edge) rotation 3.
void WorldRenderer::startViewAt(const WorldMap &map, int tileX, int tileY) {
  mapWidthCache = map.getWidth();
  mapHeightCache = map.getHeight();
  const int W = mapWidthCache, H = mapHeightCache;

  // Lower-left screen edge per rotation: 0 -> x = 0, 1 -> y = 0,
  // 2 -> x = W, 3 -> y = H
  const int distance[4] = {tileX, tileY, W - 1 - tileX, H - 1 - tileY};
  int best = 0;
  for (int r = 1; r < 4; r++)
    if (distance[r] < distance[best])
      best = r;
  viewRotation = best;

  // Centre the camera on the middle of the start tile
  int pu0, pv0, pu1, pv1;
  worldToViewVertex(tileX, tileY, pu0, pv0);
  worldToViewVertex(tileX + 1, tileY + 1, pu1, pv1);
  float u = (pu0 + pu1) * 0.5f, v = (pv0 + pv1) * 0.5f;
  const MapTile *tile = map.getTile(tileX, tileY);
  float lift = tile ? tile->height * getHeightUnitPixels() : 0.0f;
  camera.x = static_cast<int>(std::lround(-(u - v) * tileWidth * 0.5f));
  camera.y = static_cast<int>(std::lround(-(u + v) * tileHeight * 0.5f + lift));
  SDL_Log("[WorldRenderer] Start view: tile (%d,%d), rotation %d", tileX,
          tileY, viewRotation);
}

void WorldRenderer::getViewSize(int &viewU, int &viewV) const {
  bool swapped = (viewRotation % 2) == 0;
  viewU = swapped ? mapHeightCache : mapWidthCache;
  viewV = swapped ? mapWidthCache : mapHeightCache;
}

bool WorldRenderer::viewTileToWorld(int u, int v, int &x, int &y) const {
  int U, V;
  getViewSize(U, V);
  if (u < 0 || v < 0 || u >= U || v >= V)
    return false;
  int x0, y0, x1, y1;
  viewToWorldVertex(u, v, x0, y0);
  viewToWorldVertex(u + 1, v + 1, x1, y1);
  x = std::min(x0, x1);
  y = std::min(y0, y1);
  return true;
}

void WorldRenderer::getViewCentre(float &u, float &v) const {
  // The screen centre is the view origin moved by -camera
  float a = -camera.x / (tileWidth * 0.5f);  // u - v
  float b = -camera.y / (tileHeight * 0.5f); // u + v
  u = (a + b) * 0.5f;
  v = (b - a) * 0.5f;
}

void WorldRenderer::centreViewOn(float u, float v) {
  camera.x = static_cast<int>(std::lround(-(u - v) * tileWidth * 0.5f));
  camera.y = static_cast<int>(std::lround(-(u + v) * tileHeight * 0.5f));
}

void WorldRenderer::centreOnWorld(float x, float y, float height) {
  const float W = static_cast<float>(mapWidthCache), H = static_cast<float>(mapHeightCache);
  float pu, pv;
  switch (viewRotation) {
  case 0: pu = y; pv = W - x; break;
  case 1: pu = W - x; pv = H - y; break;
  case 2: pu = H - y; pv = x; break;
  default: pu = x; pv = y; break;
  }
  this->centreViewOn(pu, pv);
  camera.y += static_cast<int>(std::lround(height * getHeightUnitPixels())); // (as startViewAt)
}

float WorldRenderer::logicalCenterX() const {
  float zoom = camera.zoom > 0.0f ? camera.zoom : 1.0f;
  return camera.screenCenterX / zoom;
}

float WorldRenderer::logicalCenterY() const {
  float zoom = camera.zoom > 0.0f ? camera.zoom : 1.0f;
  return camera.screenCenterY / zoom;
}

void WorldRenderer::projectTile(int tileX, int tileY, float &screenX,
                                float &screenY) const {
  screenX = (tileX - tileY) * (tileWidth * 0.5f) + camera.x + logicalCenterX();
  screenY = (tileX + tileY) * (tileHeight * 0.5f) + camera.y + logicalCenterY();
}

void WorldRenderer::worldToScreenF(float x, float y, float height,
                                   float &screenX, float &screenY,
                                   float &depth) const {
  const float W = static_cast<float>(mapWidthCache), H = static_cast<float>(mapHeightCache);
  float pu, pv;
  switch (viewRotation) {
  case 0: pu = y; pv = W - x; break;
  case 1: pu = W - x; pv = H - y; break;
  case 2: pu = H - y; pv = x; break;
  default: pu = x; pv = y; break;
  }
  screenX = (pu - pv) * (tileWidth * 0.5f) + camera.x + logicalCenterX();
  screenY = (pu + pv) * (tileHeight * 0.5f) + camera.y + logicalCenterY() -
            height * getHeightUnitPixels();
  depth = pu + pv;
}

bool WorldRenderer::screenToWorld(float sx, float sy, const WorldMap &map,
                                  float &x, float &y) const {
  const float W = static_cast<float>(mapWidthCache), H = static_cast<float>(mapHeightCache);
  // The world point under the screen point at height h
  auto at = [&](float h, float &wx, float &wy) {
    float a = (sx - camera.x - logicalCenterX()) / (tileWidth * 0.5f);
    float b = (sy + h * getHeightUnitPixels() - camera.y - logicalCenterY()) /
              (tileHeight * 0.5f);
    float pu = (a + b) * 0.5f, pv = (b - a) * 0.5f;
    switch (viewRotation) {
    case 0: wx = W - pv; wy = pu; break;
    case 1: wx = W - pu; wy = H - pv; break;
    case 2: wx = pv; wy = H - pu; break;
    default: wx = pu; wy = pv; break;
    }
  };
  // The ground's height at a world point (its tile's corners blended); no
  // tile: far below
  auto ground = [&](float wx, float wy, bool &on) {
    const MapTile *t = map.getTile(static_cast<int>(std::floor(wx)),
                                   static_cast<int>(std::floor(wy)));
    on = t != nullptr;
    if (!t)
      return -1e9f;
    float fx = wx - std::floor(wx), fy = wy - std::floor(wy);
    float top = t->cornerHeight[CORNER_X0Y0] * (1 - fx) + t->cornerHeight[CORNER_X1Y0] * fx;
    float bottom = t->cornerHeight[CORNER_X0Y1] * (1 - fx) + t->cornerHeight[CORNER_X1Y1] * fx;
    return top * (1 - fy) + bottom * fy;
  };
  // Along the line of sight from the highest ground down: the first place
  // it meets the ground is what is seen there (the nearest to the viewer)
  const float step = 0.125f;
  float hi = map.getMaxHeight() + 1.0f, lo = map.getMinHeight() - 1.0f;
  bool on = false;
  for (float h = hi; h >= lo; h -= step) {
    float wx, wy;
    at(h, wx, wy);
    float g = ground(wx, wy, on);
    if (on && g >= h) {
      // Narrow it down between h and h + step
      float a = h, b = h + step;
      for (int i = 0; i < 8; i++) {
        float m = (a + b) * 0.5f;
        at(m, wx, wy);
        bool o;
        if (ground(wx, wy, o) >= m && o)
          a = m;
        else
          b = m;
      }
      at(a, x, y);
      return true;
    }
  }
  // Off the map: where it would be at height 0
  at(0, x, y);
  return false;
}

CompassDirection WorldRenderer::screenSide(float dx, float dy) const {
  // The world direction in view terms (u down-right, v down-left)
  float du, dv;
  switch (viewRotation) {
  case 0: du = dy; dv = -dx; break;
  case 1: du = -dx; dv = -dy; break;
  case 2: du = -dy; dv = dx; break;
  default: du = dx; dv = dy; break;
  }
  if (std::fabs(du) >= std::fabs(dv))
    return du > 0 ? CompassDirection::SE : CompassDirection::NW;
  return dv > 0 ? CompassDirection::SW : CompassDirection::NE;
}

void WorldRenderer::tileToScreen(int tileX, int tileY, int height,
                                 int &screenX, int &screenY) const {
  int pu, pv;
  worldToViewVertex(tileX, tileY, pu, pv);
  float sx, sy;
  projectTile(pu, pv, sx, sy);
  sy -= height * getHeightUnitPixels();
  screenX = static_cast<int>(std::lround(sx));
  screenY = static_cast<int>(std::lround(sy));
}

// ============================================================================
// GEOMETRY BATCHING
// ============================================================================

void WorldRenderer::addTriangle(SDL_Renderer *renderer, SDL_Texture *texture,
                                const SDL_Vertex &a, const SDL_Vertex &b,
                                const SDL_Vertex &c) {
  if (texture != batchTexture) {
    flushBatch(renderer);
    batchTexture = texture;
  }
  int base = static_cast<int>(batchVertices.size());
  batchVertices.push_back(a);
  batchVertices.push_back(b);
  batchVertices.push_back(c);
  batchIndices.push_back(base);
  batchIndices.push_back(base + 1);
  batchIndices.push_back(base + 2);
}

void WorldRenderer::flushBatch(SDL_Renderer *renderer) {
  if (!batchIndices.empty()) {
    if (SDL_RenderGeometry(renderer, batchTexture, batchVertices.data(),
                           static_cast<int>(batchVertices.size()),
                           batchIndices.data(),
                           static_cast<int>(batchIndices.size())) != 0) {
      static bool logged = false;
      if (!logged) {
        SDL_Log("[WorldRenderer] SDL_RenderGeometry failed: %s",
                SDL_GetError());
        logged = true;
      }
    }
  }
  batchVertices.clear();
  batchIndices.clear();
}

void WorldRenderer::addToBucket(int layer, int order, SDL_Texture *texture,
                                const SDL_Vertex &a, const SDL_Vertex &b,
                                const SDL_Vertex &c) {
  Bucket *bucket = nullptr;
  for (Bucket &bk : buckets) {
    if (bk.layer == layer && bk.order == order && bk.texture == texture) {
      bucket = &bk;
      break;
    }
  }
  if (!bucket) {
    buckets.push_back({layer, order, texture, {}});
    bucket = &buckets.back();
  }
  bucket->vertices.push_back(a);
  bucket->vertices.push_back(b);
  bucket->vertices.push_back(c);
}

void WorldRenderer::flushBuckets(SDL_Renderer *renderer) {
  std::sort(buckets.begin(), buckets.end(),
            [](const Bucket &a, const Bucket &b) {
              if (a.layer != b.layer)
                return a.layer < b.layer;
              return a.order < b.order;
            });
  for (Bucket &bk : buckets) {
    for (size_t i = 0; i + 2 < bk.vertices.size(); i += 3)
      addTriangle(renderer, bk.texture, bk.vertices[i], bk.vertices[i + 1],
                  bk.vertices[i + 2]);
  }
  buckets.clear();
}

// ============================================================================
// PER-MAP CACHE: vertex lighting and terrain blending (world space)
// ============================================================================

void WorldRenderer::rebuildCache(const WorldMap &map, SpriteDatabase &spriteDB) {
  const int W = map.getWidth(), H = map.getHeight();
  cache.map = &map;
  cache.generation = map.getGeneration();
  cache.cornerLight.assign(static_cast<size_t>(W) * H * 4, 1.0f);
  cache.cornerNormal.assign(static_cast<size_t>(W) * H * 12, 0.0f);
  cache.overlayStart.assign(static_cast<size_t>(W) * H + 1, 0);
  cache.overlays.clear();

  // World vertex (vx, vy) of tile corner c, relative to the tile
  static const int cdx[4] = {0, 1, 1, 0}; // X0Y0, X1Y0, X1Y1, X0Y1
  static const int cdy[4] = {0, 0, 1, 1};
  auto cornerOf = [](int dx, int dy) {
    return dy == 0 ? (dx == 0 ? CORNER_X0Y0 : CORNER_X1Y0)
                   : (dx == 0 ? CORNER_X0Y1 : CORNER_X1Y1);
  };

  // Face normal of each tile (unnormalised, z up)
  std::vector<float> fn(static_cast<size_t>(W) * H * 3);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const int *h = map.getTile(x, y)->cornerHeight;
      float dzdx = ((h[CORNER_X1Y0] + h[CORNER_X1Y1]) -
                    (h[CORNER_X0Y0] + h[CORNER_X0Y1])) * 0.5f * kHeightUnitInTiles;
      float dzdy = ((h[CORNER_X0Y1] + h[CORNER_X1Y1]) -
                    (h[CORNER_X0Y0] + h[CORNER_X1Y0])) * 0.5f * kHeightUnitInTiles;
      float *n = &fn[(static_cast<size_t>(y) * W + x) * 3];
      n[0] = -dzdx; n[1] = -dzdy; n[2] = 1.0f;
    }
  }

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      const MapTile *tile = map.getTile(x, y);
      const size_t ti = static_cast<size_t>(y) * W + x;

      // Per-corner vertex normal: average the faces of the tiles sharing
      // this vertex at the same height (a cliff splits the vertex)
      float weights[NUM_TERRAIN_TYPES][4] = {};
      bool ownBlends = spriteDB.terrainBlends(tile->terrainType);
      for (int c = 0; c < 4; c++) {
        int vx = x + cdx[c], vy = y + cdy[c];
        int myH = tile->cornerHeight[c];
        float nx = 0, ny = 0, nz = 0;
        int blendCount = 0;
        int typeCount[NUM_TERRAIN_TYPES] = {};
        for (int oy = vy - 1; oy <= vy; oy++) {
          for (int ox = vx - 1; ox <= vx; ox++) {
            const MapTile *o = map.getTile(ox, oy);
            if (!o)
              continue;
            int oc = cornerOf(vx - ox, vy - oy);
            if (o->cornerHeight[oc] == myH) {
              const float *n = &fn[(static_cast<size_t>(oy) * W + ox) * 3];
              nx += n[0]; ny += n[1]; nz += n[2];
            }
            if (o->terrainType < NUM_TERRAIN_TYPES &&
                spriteDB.terrainBlends(o->terrainType)) {
              typeCount[o->terrainType]++;
              blendCount++;
            }
          }
        }
        cache.cornerLight[ti * 4 + c] = groundLight(nx, ny, nz);
        float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nlen > 0) {
          cache.cornerNormal[ti * 12 + c * 3 + 0] = nx / nlen;
          cache.cornerNormal[ti * 12 + c * 3 + 1] = ny / nlen;
          cache.cornerNormal[ti * 12 + c * 3 + 2] = nz / nlen;
        }
        if (ownBlends && blendCount > 0) {
          for (int t = 0; t < NUM_TERRAIN_TYPES; t++)
            weights[t][c] = typeCount[t] / float(blendCount);
        }
      }

      // Blend overlays in ascending terrain order. Each overlay's alpha is
      // its weight relative to everything painted so far, which reproduces
      // the exact per-vertex mix: own * w0 + t1 * w1 + ...
      cache.overlayStart[ti] = static_cast<uint32_t>(cache.overlays.size());
      if (ownBlends && tile->terrainType < NUM_TERRAIN_TYPES) {
        float painted[4];
        for (int c = 0; c < 4; c++)
          painted[c] = weights[tile->terrainType][c];
        for (int t = 0; t < NUM_TERRAIN_TYPES; t++) {
          if (t == tile->terrainType)
            continue;
          if (weights[t][0] + weights[t][1] + weights[t][2] + weights[t][3] <= 0)
            continue;
          TileOverlay ov;
          ov.terrain = static_cast<uint8_t>(t);
          for (int c = 0; c < 4; c++) {
            float w = weights[t][c];
            float a = (painted[c] + w) > 0 ? w / (painted[c] + w) : 0.0f;
            ov.alpha[c] = static_cast<uint8_t>(std::lround(a * 255.0f));
            painted[c] += w;
          }
          cache.overlays.push_back(ov);
        }
      }
    }
  }
  cache.overlayStart[static_cast<size_t>(W) * H] =
      static_cast<uint32_t>(cache.overlays.size());
}

// ============================================================================
// TERRAIN RENDERING
// ============================================================================

void WorldRenderer::renderTerrain(SDL_Renderer *renderer, const WorldMap &map,
                                  SpriteDatabase &spriteDB) {
  if (!renderer)
    return;

  int mapWidth = map.getWidth();
  int mapHeight = map.getHeight();

  if (mapWidth == 0 || mapHeight == 0)
    return;

  mapWidthCache = mapWidth;
  mapHeightCache = mapHeight;
  if (cache.map != &map || cache.generation != map.getGeneration())
    rebuildCache(map, spriteDB);

  // View grid size (u across, v deep)
  const bool swapped = (viewRotation % 2) == 0;
  const int viewU = swapped ? mapHeight : mapWidth;
  const int viewV = swapped ? mapWidth : mapHeight;

  // The world tile under view tile (u, v), with its corner heights, world
  // corner indices and world vertices in view order: top, right, bottom, left
  enum { VT = 0, VR, VB, VL };
  struct ViewTile {
    const MapTile *tile = nullptr;
    int x = 0, y = 0;
    int h[4] = {0, 0, 0, 0};
    int corner[4] = {0, 0, 0, 0};
    int wx[4] = {0, 0, 0, 0}, wy[4] = {0, 0, 0, 0};
  };
  auto viewTile = [&](int u, int v) {
    ViewTile vt;
    if (u < 0 || v < 0 || u >= viewU || v >= viewV)
      return vt;
    const int du[4] = {0, 1, 1, 0}, dv[4] = {0, 0, 1, 1};
    for (int i = 0; i < 4; i++)
      viewToWorldVertex(u + du[i], v + dv[i], vt.wx[i], vt.wy[i]);
    vt.x = std::min(vt.wx[VT], vt.wx[VB]);
    vt.y = std::min(vt.wy[VT], vt.wy[VB]);
    vt.tile = map.getTile(vt.x, vt.y);
    if (!vt.tile)
      return vt;
    for (int i = 0; i < 4; i++) {
      bool x1 = vt.wx[i] != vt.x, y1 = vt.wy[i] != vt.y;
      vt.corner[i] = y1 ? (x1 ? CORNER_X1Y1 : CORNER_X0Y1)
                        : (x1 ? CORNER_X1Y0 : CORNER_X0Y0);
      vt.h[i] = vt.tile->cornerHeight[vt.corner[i]];
    }
    return vt;
  };

  // World direction of the view axes, for the normal-map debug view
  int ox0, oy0, uxw, uyw, vxw, vyw;
  viewToWorldVertex(0, 0, ox0, oy0);
  viewToWorldVertex(1, 0, uxw, uyw);
  viewToWorldVertex(0, 1, vxw, vyw);

  const float halfW = tileWidth * 0.5f;
  const float halfH = tileHeight * 0.5f;
  const float unitPx = getHeightUnitPixels();

  // Visible area in logical pixels
  const float viewW = logicalCenterX() * 2.0f;
  const float viewH = logicalCenterY() * 2.0f;
  const int lowestHeight = map.getMinHeight();

  batchVertices.clear();
  batchIndices.clear();
  batchTexture = nullptr;
  buckets.clear();

  // Untextured triangles (debug colours) blend with the draw blend mode
  SDL_BlendMode previousBlend;
  SDL_GetRenderDrawBlendMode(renderer, &previousBlend);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  // Zoomed in past 1x, map art comes from its upscaled textures
  const bool hires = RenderSettings::worldZoomedIn;

  struct TexInfo {
    SDL_Texture *texture = nullptr;
    float tilesPerTexture = 1.0f;
  };
  auto textureFor = [&](int terrainType) {
    TexInfo info;
    if (debugTerrainIds)
      return info;
    info.texture = spriteDB.getTerrainTexture(renderer, terrainType, hires);
    if (!info.texture && hires)
      info.texture = spriteDB.getTerrainTexture(renderer, terrainType);
    if (info.texture) {
      int texW = 0;
      ArtScaler::querySize(info.texture, &texW, nullptr);
      info.tilesPerTexture = std::max(1.0f, std::round(texW / kTexelsPerTile));
    }
    return info;
  };
  auto baseColor = [&](int terrain, const TexInfo &tex) {
    if (debugTerrainIds)
      return terrainColor(kDebugColors, terrain);
    if (tex.texture)
      return SDL_Color{255, 255, 255, 255};
    return terrainColor(kFallbackColors, terrain);
  };

  // Optional draw distance: the view tile under the centre of the screen
  // (screen centre = view origin offset by -camera)
  float centreU = 0.0f, centreV = 0.0f;
  if (drawDistance > 0) {
    float a = -camera.x / halfW; // u - v
    float b = -camera.y / halfH; // u + v
    centreU = (a + b) * 0.5f;
    centreV = (b - a) * 0.5f;
  }

  // Back-to-front, one view diagonal (u + v = d) at a time
  for (int d = 0; d <= viewU + viewV - 2; d++) {
    int uStart = std::max(0, d - (viewV - 1));
    int uEnd = std::min(d, viewU - 1);

    for (int u = uStart; u <= uEnd; u++) {
      int v = d - u;
      if (drawDistance > 0 && (std::fabs(u - centreU) > drawDistance ||
                               std::fabs(v - centreV) > drawDistance))
        continue;
      ViewTile vt = viewTile(u, v);
      const MapTile *tile = vt.tile;
      if (!tile)
        continue;

      const int *h = vt.h;
      const int x = vt.x, y = vt.y;
      const size_t ti = static_cast<size_t>(y) * mapWidth + x;
      float sx, sy;
      projectTile(u, v, sx, sy);

      // Cull: the column spans from the highest corner down to the lowest
      // point on the map
      int topHeight = *std::max_element(h, h + 4);
      float top = sy - topHeight * unitPx;
      float bottom = sy + tileHeight - lowestHeight * unitPx;
      if (sx + halfW < 0.0f || sx - halfW > viewW || top > viewH ||
          bottom < 0.0f)
        continue;

      // Screen positions of the four corners
      SDL_FPoint p[4] = {{sx, sy - h[VT] * unitPx},
                         {sx + halfW, sy + halfH - h[VR] * unitPx},
                         {sx, sy + tileHeight - h[VB] * unitPx},
                         {sx - halfW, sy + halfH - h[VL] * unitPx}};

      // --- Substrate Layer: cliff faces toward the viewer ---
      // A front edge drops to the matching edge of the neighbour in front.
      // If that neighbour is higher, its face points away and is hidden.
      uint8_t substrate = tile->substrate;
      TexInfo wallTex = textureFor(substrate);
      SDL_Color wallBase = baseColor(substrate, wallTex);

      // face: 0 = toward +u (lower right on screen), 1 = toward +v
      auto drawWall = [&](SDL_FPoint a, int ha, SDL_FPoint b, int hb, int na,
                          int nb, int face, float light) {
        int da = std::max(0, ha - std::min(ha, na));
        int db = std::max(0, hb - std::min(hb, nb));
        if (da == 0 && db == 0)
          return;
        SDL_Color c = shade(wallBase, shadingMode == 1 ? 1.0f : light);
        SDL_Texture *texture = wallTex.texture;
        if (shadingMode == 2) {
          // World-space wall normal = the direction the face points
          float wx = face == 0 ? float(uxw - ox0) : float(vxw - ox0);
          float wy = face == 0 ? float(uyw - oy0) : float(vyw - oy0);
          c = encodeNormal(wx, wy, 0.0f);
          texture = nullptr;
        }
        float inv = 1.0f / wallTex.tilesPerTexture;

        // SDL_RenderGeometry rejects the whole batch if any UV leaves
        // [0, 1], so tall cliffs are split into bands of at most one
        // texture repeat
        float vPerUnit = kHeightUnitInTiles * inv;
        int bandUnits = std::max(1, static_cast<int>(1.0f / vPerUnit));
        int maxDrop = std::max(da, db);
        for (int topU = 0; topU < maxDrop; topU += bandUnits) {
          int bot = std::min(topU + bandUnits, maxDrop);
          int ta = std::min(topU, da), ba = std::min(bot, da);
          int tb = std::min(topU, db), bb = std::min(bot, db);
          auto tv = [&](int units) {
            return std::min(1.0f, (units - topU) * vPerUnit);
          };
          SDL_Vertex va = {{a.x, a.y + ta * unitPx}, c, {0.0f, tv(ta)}};
          SDL_Vertex vb = {{b.x, b.y + tb * unitPx}, c, {inv, tv(tb)}};
          SDL_Vertex vb2 = {{b.x, b.y + bb * unitPx}, c, {inv, tv(bb)}};
          SDL_Vertex va2 = {{a.x, a.y + ba * unitPx}, c, {0.0f, tv(ba)}};
          if (tb != bb)
            addToBucket(0, 0, texture, va, vb, vb2);
          if (ta != ba)
            addToBucket(0, 0, texture, va, vb2, va2);
        }
      };

      // Neighbour at u+1 shares our right/bottom edge as its top/left edge.
      // Past the map border there is no neighbour and nothing is drawn.
      ViewTile nu = viewTile(u + 1, v);
      if (nu.tile)
        drawWall(p[VR], h[VR], p[VB], h[VB], nu.h[VT], nu.h[VL], 0, kRightCliffLight);

      // Neighbour at v+1 shares our left/bottom edge as its top/right edge
      ViewTile nv = viewTile(u, v + 1);
      if (nv.tile)
        drawWall(p[VL], h[VL], p[VB], h[VB], nv.h[VT], nv.h[VR], 1, kLeftCliffLight);

      // --- Floor Layer: lit, sloped surface plus terrain blends ---
      float light[4];
      for (int i = 0; i < 4; i++)
        light[i] = elevationEnabled ? cache.cornerLight[ti * 4 + vt.corner[i]]
                                    : kGroundLight;

      // Split along the flatter diagonal so ridges and valleys keep shape
      const int(*tris)[3];
      static const int splitTB[2][3] = {{VT, VR, VB}, {VT, VB, VL}};
      static const int splitLR[2][3] = {{VT, VR, VL}, {VL, VR, VB}};
      tris = std::abs(h[VT] - h[VB]) <= std::abs(h[VL] - h[VR]) ? splitTB
                                                                 : splitLR;

      // Paint one terrain over this tile with per-corner alpha
      auto paint = [&](int layer, int terrain, const uint8_t alpha[4],
                       float checker) {
        TexInfo tex = textureFor(terrain);
        SDL_Color base = shade(baseColor(terrain, tex), checker);
        float k = tex.tilesPerTexture;
        float baseU = std::fmod(float(x), k) / k;
        float baseV = std::fmod(float(y), k) / k;
        SDL_Vertex vtx[4];
        SDL_Texture *texture = tex.texture;
        for (int i = 0; i < 4; i++) {
          SDL_Color c = shade(base, shadingMode == 1 ? 1.0f : light[i]);
          if (shadingMode == 2) {
            const float *n = &cache.cornerNormal[ti * 12 + vt.corner[i] * 3];
            c = encodeNormal(n[0], n[1], n[2]);
            texture = nullptr;
          }
          c.a = alpha ? alpha[vt.corner[i]] : 255;
          vtx[i] = {p[i], c,
                    {baseU + (vt.wx[i] - x) / k, baseV + (vt.wy[i] - y) / k}};
        }
        if (shadingMode == 2 && layer == 1)
          return; // blends don't change the normal
        for (int t = 0; t < 2; t++)
          addToBucket(layer, terrain, texture, vtx[tris[t][0]],
                      vtx[tris[t][1]], vtx[tris[t][2]]);
      };

      float checker = (debugTerrainIds && ((x + y) & 1)) ? kCheckerShade : 1.0f;
      paint(0, tile->terrainType, nullptr, checker);
      for (uint32_t o = cache.overlayStart[ti]; o < cache.overlayStart[ti + 1];
           o++) {
        const TileOverlay &ov = cache.overlays[o];
        paint(1, ov.terrain, ov.alpha, checker);
      }

      // --- Paths: the path type's frame for this tile, drawn like the
      // original draws sprites: the frame's anchor on the tile centre.
      // Frames (paths/<type>/idle/N, measured from the art):
      //   1-4  ramps, by which two corners are raised (in view terms):
      //        1 bottom+left, 2 right+bottom, 3 top+left, 4 top+right
      //   5-20 flat, 5 + (15 - kerb mask); a kerb is drawn on each edge
      //        with no path beyond it at the same height
      //        (mask bits: NW 8, NE 4, SE 2, SW 1)
      int pathType = map.getPathType(x, y);
      if (pathType >= 0 && !debugTerrainIds) {
        auto connected = [&](int du, int dv, int a, int na, int b, int nb) {
          ViewTile n = viewTile(u + du, v + dv);
          return n.tile && map.getPathType(n.x, n.y) >= 0 && h[a] == n.h[na] &&
                 h[b] == n.h[nb];
        };
        int lowest = *std::min_element(h, h + 4);
        bool raised[4];
        for (int i = 0; i < 4; i++)
          raised[i] = h[i] > lowest;

        int frame;
        if (raised[VB] && raised[VL] && !raised[VT] && !raised[VR]) {
          frame = 1;
        } else if (raised[VR] && raised[VB] && !raised[VT] && !raised[VL]) {
          frame = 2;
        } else if (raised[VT] && raised[VL] && !raised[VR] && !raised[VB]) {
          frame = 3;
        } else if (raised[VT] && raised[VR] && !raised[VB] && !raised[VL]) {
          frame = 4;
        } else {
          int kerbs = 0;
          if (!connected(-1, 0, VT, VR, VL, VB))
            kerbs |= 8; // NW
          if (!connected(0, -1, VT, VL, VR, VB))
            kerbs |= 4; // NE
          if (!connected(1, 0, VR, VT, VB, VL))
            kerbs |= 2; // SE
          if (!connected(0, 1, VL, VT, VB, VR))
            kerbs |= 1; // SW
          frame = 5 + (15 - kerbs);
        }

        const SpriteDatabase::Sprite &art = spriteDB.getPathSprite(
            renderer, map.getPathTypes()[pathType], frame, hires);
        SDL_Texture *pathTexture =
            hires && art.hiTexture ? art.hiTexture : art.texture;
        if (pathTexture) {
          // Tile centre on screen, at the average corner height
          float centreH = (h[0] + h[1] + h[2] + h[3]) * 0.25f;
          float cx = sx, cy = sy + halfH - centreH * unitPx;
          float k = tileWidth / 64.0f; // art is drawn for 64 px tiles
          float x0 = cx - art.anchorX * k, y0 = cy - art.anchorY * k;
          float x1 = x0 + art.width * k, y1 = y0 + art.height * k;
          SDL_Color white = {255, 255, 255, 255};
          SDL_Vertex q0 = {{x0, y0}, white, {0, 0}};
          SDL_Vertex q1 = {{x1, y0}, white, {1, 0}};
          SDL_Vertex q2 = {{x1, y1}, white, {1, 1}};
          SDL_Vertex q3 = {{x0, y1}, white, {0, 1}};
          addToBucket(2, pathType, pathTexture, q0, q1, q2);
          addToBucket(2, pathType, pathTexture, q0, q2, q3);
        }
      }

      // --- Grid (Ctrl+G): this tile's two back edges, at its own corner
      // heights so it follows slopes and sits on top of cliffs. Same lines
      // as the original's tiles.ztd grid bitmaps (n*.bmp / e*.bmp): 2 px,
      // an upper and a lower tone, under any objects on the tile
      if (gridVisible || toolGrid) {
        auto edge = [&](SDL_FPoint a, SDL_FPoint b, SDL_Color upper,
                        SDL_Color lower) {
          auto band = [&](float top, SDL_Color c) {
            SDL_Vertex v0 = {{a.x, a.y + top}, c, {0, 0}};
            SDL_Vertex v1 = {{b.x, b.y + top}, c, {0, 0}};
            SDL_Vertex v2 = {{b.x, b.y + top + 1.0f}, c, {0, 0}};
            SDL_Vertex v3 = {{a.x, a.y + top + 1.0f}, c, {0, 0}};
            addToBucket(3, 0, nullptr, v0, v1, v2);
            addToBucket(3, 0, nullptr, v0, v2, v3);
          };
          band(-1.0f, upper);
          band(0.0f, lower);
        };
        edge(p[VL], p[VT], kGridNorthUpper, kGridNorthLower); // n*.bmp
        edge(p[VT], p[VR], kGridEastUpper, kGridEastLower);   // e*.bmp
      }
    }
    flushBuckets(renderer);
  }

  flushBatch(renderer);
  batchTexture = nullptr;
  SDL_SetRenderDrawBlendMode(renderer, previousBlend);
}

void WorldRenderer::drawPathPiece(SDL_Renderer *renderer, SpriteDatabase &spriteDB,
                                  const std::string &type, int tileX, int tileY,
                                  const int cornerH[4],
                                  const std::function<bool(int nx, int ny)> &connected,
                                  const SDL_Color *tint) const {
  enum { VT = 0, VR, VB, VL };
  // Each world corner's place in view terms (top, right, bottom, left)
  const int dx[4] = {0, 1, 1, 0}, dy[4] = {0, 0, 1, 1}; // X0Y0, X1Y0, X1Y1, X0Y1
  int pu[4], pv[4];
  for (int c = 0; c < 4; c++)
    worldToViewVertex(tileX + dx[c], tileY + dy[c], pu[c], pv[c]);
  int u0 = *std::min_element(pu, pu + 4), v0 = *std::min_element(pv, pv + 4);
  int h[4] = {0, 0, 0, 0};
  for (int c = 0; c < 4; c++) {
    int du = pu[c] - u0, dv = pv[c] - v0;
    int slot = du == 0 ? (dv == 0 ? VT : VL) : (dv == 0 ? VR : VB);
    h[slot] = cornerH[c];
  }
  int lowest = *std::min_element(h, h + 4);
  bool raised[4];
  for (int i = 0; i < 4; i++)
    raised[i] = h[i] > lowest;
  int frame;
  if (raised[VB] && raised[VL] && !raised[VT] && !raised[VR])
    frame = 1;
  else if (raised[VR] && raised[VB] && !raised[VT] && !raised[VL])
    frame = 2;
  else if (raised[VT] && raised[VL] && !raised[VR] && !raised[VB])
    frame = 3;
  else if (raised[VT] && raised[VR] && !raised[VB] && !raised[VL])
    frame = 4;
  else {
    // Kerbs: each world neighbour's view side
    int kerbs = 0;
    const int nx[4] = {0, 1, 0, -1}, ny[4] = {-1, 0, 1, 0};
    for (int n = 0; n < 4; n++) {
      int qu, qv, ru, rv;
      worldToViewVertex(tileX + nx[n], tileY + ny[n], qu, qv);
      worldToViewVertex(tileX + nx[n] + 1, tileY + ny[n] + 1, ru, rv);
      int nu = std::min(qu, ru), nv = std::min(qv, rv);
      // (the neighbour's view top corner, from two of its corners)
      int a, b;
      worldToViewVertex(tileX + nx[n] + 1, tileY + ny[n], a, b);
      nu = std::min(nu, a);
      nv = std::min(nv, b);
      worldToViewVertex(tileX + nx[n], tileY + ny[n] + 1, a, b);
      nu = std::min(nu, a);
      nv = std::min(nv, b);
      if (connected(tileX + nx[n], tileY + ny[n]))
        continue;
      if (nu < u0)
        kerbs |= 8; // NW
      else if (nv < v0)
        kerbs |= 4; // NE
      else if (nu > u0)
        kerbs |= 2; // SE
      else
        kerbs |= 1; // SW
    }
    frame = 5 + (15 - kerbs);
  }
  const bool hires = RenderSettings::worldZoomedIn;
  const SpriteDatabase::Sprite &art = spriteDB.getPathSprite(renderer, type, frame, hires);
  SDL_Texture *texture = hires && art.hiTexture ? art.hiTexture : art.texture;
  if (!texture)
    return;
  float centreH = (h[0] + h[1] + h[2] + h[3]) * 0.25f;
  float cx, cy, depth;
  worldToScreenF(tileX + 0.5f, tileY + 0.5f, centreH, cx, cy, depth);
  float k = tileWidth / 64.0f;
  float x0 = cx - art.anchorX * k, y0 = cy - art.anchorY * k;
  float x1 = x0 + art.width * k, y1 = y0 + art.height * k;
  SDL_Color c = tint ? *tint : SDL_Color{255, 255, 255, 255};
  SDL_FRect dst = {x0, y0, x1 - x0, y1 - y0};
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  if (tint) {
    SDL_SetTextureColorMod(texture, tint->r, tint->g, tint->b);
    SDL_SetTextureAlphaMod(texture, tint->a);
  }
  SDL_RenderCopyF(renderer, texture, nullptr, &dst);
  if (tint) {
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
  }
}

void WorldRenderer::drawBox(SDL_Renderer *renderer, SpriteDatabase &spriteDB, float x0, float y0,
                            float x1, float y1, float z0, float z1, int terrain, bool top,
                            Uint8 alpha) const {
  if (z1 <= z0)
    return;
  const bool hires = RenderSettings::worldZoomedIn;
  SDL_Texture *tex = spriteDB.getTerrainTexture(renderer, terrain, hires);
  if (!tex && hires)
    tex = spriteDB.getTerrainTexture(renderer, terrain);
  float tpt = 1.0f;
  if (tex) {
    int texW = 0;
    ArtScaler::querySize(tex, &texW, nullptr);
    tpt = std::max(1.0f, std::round(texW / kTexelsPerTile));
  }
  // The base's corners in order round it, on screen
  const float wx[4] = {x0, x1, x1, x0}, wy[4] = {y0, y0, y1, y1};
  SDL_FPoint lo[4], hi[4];
  float d;
  for (int i = 0; i < 4; i++) {
    worldToScreenF(wx[i], wy[i], z0, lo[i].x, lo[i].y, d);
    worldToScreenF(wx[i], wy[i], z1, hi[i].x, hi[i].y, d);
  }
  // The front corner (lowest on screen) and the two beside it
  int b = 0;
  for (int i = 1; i < 4; i++)
    if (lo[i].y > lo[b].y)
      b = i;
  int p = (b + 3) % 4, n = (b + 1) % 4;
  int left = lo[p].x < lo[n].x ? p : n, right = left == p ? n : p;
  auto face = [&](int a, int c, float light) {
    SDL_Color col = shade(SDL_Color{255, 255, 255, 255}, light);
    col.a = alpha;
    float len = std::hypot(wx[a] - wx[c], wy[a] - wy[c]);
    float u = std::min(1.0f, len / tpt);
    float units = z1 - z0;
    float vPerUnit = kHeightUnitInTiles / tpt;
    // Bands of at most one texture repeat (UVs must stay in 0..1)
    for (float t = 0; t < units; t += std::max(0.25f, 1.0f / vPerUnit)) {
      float t1 = std::min(units, t + std::max(0.25f, 1.0f / vPerUnit));
      float ya = (t1 - t) * vPerUnit;
      SDL_FPoint pa0, pa1, pc0, pc1;
      worldToScreenF(wx[a], wy[a], z1 - t, pa0.x, pa0.y, d);
      worldToScreenF(wx[a], wy[a], z1 - t1, pa1.x, pa1.y, d);
      worldToScreenF(wx[c], wy[c], z1 - t, pc0.x, pc0.y, d);
      worldToScreenF(wx[c], wy[c], z1 - t1, pc1.x, pc1.y, d);
      SDL_Vertex v[4] = {{pa0, col, {0, 0}}, {pc0, col, {u, 0}}, {pc1, col, {u, std::min(1.0f, ya)}},
                         {pa1, col, {0, std::min(1.0f, ya)}}};
      int idx[6] = {0, 1, 2, 0, 2, 3};
      SDL_RenderGeometry(renderer, tex, v, 4, idx, 6);
    }
  };
  face(left, b, kLeftCliffLight);
  face(b, right, kRightCliffLight);
  if (top) {
    SDL_Color col{255, 255, 255, alpha};
    SDL_Vertex v[4];
    for (int i = 0; i < 4; i++)
      v[i] = {hi[i], col, {i == 1 || i == 2 ? 1.0f / tpt : 0.0f, i >= 2 ? 1.0f / tpt : 0.0f}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, tex, v, 4, idx, 6);
  }
}
