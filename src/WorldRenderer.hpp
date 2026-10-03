#ifndef WORLD_RENDERER_HPP
#define WORLD_RENDERER_HPP

#include "CompassDirection.hpp"
#include "SpriteDatabase.hpp"
#include "WorldMap.hpp"
#include <SDL2/SDL.h>
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
  void toggleElevation() { elevationEnabled = !elevationEnabled; }
  bool isElevationEnabled() const { return elevationEnabled; }

  // Shading debug modes for comparison against the original:
  // 0 = lit (normal), 1 = unlit textures, 2 = world normals as colours
  void setShadingMode(int mode) { shadingMode = mode; }

  // Optional draw distance in tiles from the centre of the screen, for slow
  // machines (a future in-game setting; the original never had one).
  // 0 = draw the whole map.
  void setDrawDistance(int tiles) { drawDistance = tiles < 0 ? 0 : tiles; }
  int getDrawDistance() const { return drawDistance; }

  // Debug features
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
  // layer 0 = cliffs and surfaces, layer 1 = blend overlays by terrain type
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
