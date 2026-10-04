#ifndef WORLD_RENDERER_HPP
#define WORLD_RENDERER_HPP

#include "CompassDirection.hpp"
#include "SpriteDatabase.hpp"
#include "WorldMap.hpp"
#include <SDL2/SDL.h>
#include <functional>
#include <string>
#include <vector>

// ============================================================================
// WORLD RENDERER - Original ZT1 Engine Architecture
// ============================================================================
// PURPOSE: Render the world using isometric projection
// SEPARATION: Pure rendering - reads WorldMap data, never modifies it
// DATA SOURCE: WorldMap for tile data, SpriteDatabase for textures
// ============================================================================

struct Camera {
  int x;             // Camera position X (world space offset)
  int y;             // Camera position Y (world space offset)
  float zoom;        // Zoom level (1.0 = normal)
  int screenCenterX; // Screen center point X
  int screenCenterY; // Screen center point Y

  Camera() : x(0), y(0), zoom(1.0f), screenCenterX(640), screenCenterY(360) {}
};

class WorldRenderer {
public:
  WorldRenderer();
  ~WorldRenderer();

  // Set camera position and zoom
  void setCamera(const Camera &cam);
  Camera &getCamera() { return camera; }

  // Render terrain layer
  void renderTerrain(SDL_Renderer *renderer, const WorldMap &map,
                     SpriteDatabase &spriteDB);

  // Isometric coordinate conversion. Returns the screen position of world
  // grid vertex (tileX, tileY) at the given height, in logical (pre-zoom)
  // pixels, for the current view rotation. Uses the dimensions of the map
  // last passed to renderTerrain.
  void tileToScreen(int tileX, int tileY, int height, int &screenX,
                    int &screenY) const;
  // A world position in tiles (fractional) at a height in height units, in
  // logical pixels; depth grows towards the viewer (for drawing order)
  void worldToScreenF(float x, float y, float height, float &screenX,
                      float &screenY, float &depth) const;
  // Which screen side a world direction points to (NE, SE, SW, NW) in the
  // current rotation: what an object facing it, or a fence on that side
  // of its tile, is drawn as
  CompassDirection screenSide(float dx, float dy) const;
  // The ground under a logical screen point: world position in tiles
  // (fractional), following the ground's height. False off the map.
  bool screenToWorld(float sx, float sy, const WorldMap &map, float &x,
                     float &y) const;

  // View rotation in 90 degree steps. 0 = the game's default (north) view,
  // where the map's x = 0 edge faces the lower left of the screen.
  int getViewRotation() const { return viewRotation; }
  void rotateView(int steps);

  // Set the view the way the original game opens a map: rotated so the
  // map edge nearest the start tile (the entrance side) faces the lower
  // left, with the camera centred on that tile
  void startViewAt(const WorldMap &map, int tileX, int tileY);

  // Tile size configuration (matching original ZT1: 64x32)
  int getTileWidth() const { return tileWidth; }
  int getTileHeight() const { return tileHeight; }
  void setTileSize(int width, int height);

  // Elevation scale: pixels per height unit at the default 64x32 tile size
  // (scaled with tile size)
  int getElevationScale() const { return elevationScale; }
  void setElevationScale(int scale) { elevationScale = scale; }
  float getHeightUnitPixels() const;
  // A path piece drawn at given corner heights (world order X0Y0, X1Y0,
  // X1Y1, X0Y1), picking its frame as the terrain pass does (a ramp by its
  // raised corners, else flat with a kerb on each edge 'open' says has no
  // path beyond). For elevated walkways.
  // A box standing in the world (walkway pillars, a deck slab's edge): its
  // two faces toward the viewer, in a terrain's texture (concrete: the one
  // ZT1 faces its cliffs with) lit as the cliffs are; the top too if asked
  void drawBox(SDL_Renderer *renderer, SpriteDatabase &spriteDB, float x0, float y0, float x1,
               float y1, float z0, float z1, int terrain = 14, bool top = false,
               Uint8 alpha = 255) const;
  void drawPathPiece(SDL_Renderer *renderer, SpriteDatabase &spriteDB, const std::string &type,
                     int tileX, int tileY, const int cornerH[4],
                     const std::function<bool(int nx, int ny)> &connected, const SDL_Color *tint) const;
  void toggleElevation() { elevationEnabled = !elevationEnabled; }
  bool isElevationEnabled() const { return elevationEnabled; }

  // Shading debug modes for comparison against the original:
  // 0 = lit (normal), 1 = unlit textures, 2 = world normals as colours
  void setShadingMode(int mode) { shadingMode = mode; }

  // View grid (u across, v deep) for the map last drawn, the world tile
  // under a view tile, and the view position at the centre of the screen
  void getViewSize(int &viewU, int &viewV) const;
  bool viewTileToWorld(int u, int v, int &x, int &y) const;
  void getViewCentre(float &u, float &v) const;
  // Centre the screen on a view position (rotation-aware)
  void centreViewOn(float u, float v);
  // Centre the screen on a world position (tiles)
  // (height: the ground's there, so raised ground lands in the middle too)
  void centreOnWorld(float x, float y, float height = 0.0f);

  // Optional draw distance in tiles from the centre of the screen, for slow
  // machines (a future in-game setting; the original never had one).
  // 0 = draw the whole map.
  void setDrawDistance(int tiles) { drawDistance = tiles < 0 ? 0 : tiles; }
  int getDrawDistance() const { return drawDistance; }

  // Debug features
  // Tile grid (the original's Ctrl+G), off by default like the original
  void toggleGrid() { gridVisible = !gridVisible; }
  bool isGridVisible() const { return gridVisible || toolGrid; }
  // The grid shown while a tool needs it (laying fence), on top of the
  // player's own Ctrl+G
  void setToolGrid(bool on) { toolGrid = on; }

  void toggleTerrainDebug() { debugTerrainIds = !debugTerrainIds; }
  bool isTerrainDebugEnabled() const { return debugTerrainIds; }

private:
  Camera camera;

  // Isometric tile dimensions (pixels)
  int tileWidth;
  int tileHeight;

  // Elevation rendering
  int elevationScale; // Pixels per height unit at 64x32 tiles (default: 16,
                      // calibrated against the under.zoo preview)
  bool elevationEnabled;

  int viewRotation = 0;
  int drawDistance = 0;
  bool gridVisible = false;
  bool toolGrid = false;
  int shadingMode = 0;
  int mapWidthCache = 0;
  int mapHeightCache = 0;

  // World grid vertex for view grid vertex (pu, pv), and back
  void viewToWorldVertex(int pu, int pv, int &x, int &y) const;
  void worldToViewVertex(int x, int y, int &pu, int &pv) const;

  // Debug flags
  bool debugTerrainIds;

  // Batched triangle geometry (one SDL_RenderGeometry call per flush).
  // A batch holds one texture (or none); switching texture flushes.
  std::vector<SDL_Vertex> batchVertices;
  std::vector<int> batchIndices;
  SDL_Texture *batchTexture = nullptr;

  // Per-map data that only changes when the map does, in world space
  struct TileOverlay {
    uint8_t terrain;
    uint8_t alpha[4]; // per TileCorner
  };
  struct TerrainCache {
    const WorldMap *map = nullptr;
    uint32_t generation = 0;
    std::vector<float> cornerLight;      // 4 per tile, per TileCorner
    std::vector<float> cornerNormal;     // 4 x (x, y, z) per tile, unit
    std::vector<uint32_t> overlayStart;  // per tile, into overlays (+1 end)
    std::vector<TileOverlay> overlays;   // terrain blends painted on top
  } cache;
  void rebuildCache(const WorldMap &map, SpriteDatabase &spriteDB);

  // Triangles for one draw diagonal, grouped so each texture is drawn once:
  // layer 0 = cliffs and surfaces, 1 = terrain blends, 2 = paths, 3 = grid
  struct Bucket {
    int layer;
    int order;
    SDL_Texture *texture;
    std::vector<SDL_Vertex> vertices;
  };
  std::vector<Bucket> buckets;
  void addToBucket(int layer, int order, SDL_Texture *texture,
                   const SDL_Vertex &a, const SDL_Vertex &b,
                   const SDL_Vertex &c);
  void flushBuckets(SDL_Renderer *renderer);

  // Screen center in logical coordinates, so zoom scales around the
  // middle of the window instead of the top-left corner
  float logicalCenterX() const;
  float logicalCenterY() const;

  // View grid vertex (tileX, tileY) at height 0, in logical pixels
  void projectTile(int tileX, int tileY, float &screenX, float &screenY) const;

  void addTriangle(SDL_Renderer *renderer, SDL_Texture *texture,
                   const SDL_Vertex &a, const SDL_Vertex &b,
                   const SDL_Vertex &c);
  void flushBatch(SDL_Renderer *renderer);
};

#endif // WORLD_RENDERER_HPP
