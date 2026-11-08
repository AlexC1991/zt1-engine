#ifndef GPU_SURFACES_D3D11_HPP
#define GPU_SURFACES_D3D11_HPP

#include <array>
#include <cstdint>
#include <vector>

#include <SDL2/SDL.h>

// ours: the day and night lighting's receiving surfaces (DayNightLighting's
// drawSurfaceLighting) shaded on the graphics card, on SDL's Direct3D 11
// renderer. The scenery faces stay on the graphics card (setFaces, only when
// they change: a projection, the lamps' light, a sun map); a frame sends only
// what's drawn in what order, each shape's sky colour and outline, and which
// corners a moving actor's shadow may reach (their shadow asked of the sun
// maps on the graphics card, GpuSunD3D11). The colour is the CPU's
// arithmetic (GpuLightingShaders' surfaceGeometry, the same as the OpenGL
// experiment's): the sky colour by the sun's angle, the shadow, the lamps'
// light, clipped to the terrain and the shape's own sprite.
namespace GpuSurfacesD3D11 {

struct alignas(16) Face {
  float world[4][4]{};  // corner xyz, terrain clearance
  float screen[4][4]{}; // corner x, y (camera-independent), 0, 0
  float normal[4]{};    // xyz, visible (1 or 0)
  float local[4]{};     // an actor's: the lamps' light rgb (0-1) (the scenery's: FaceLight)
  uint32_t info[4]{};   // the shape's index (Shape), 0, 0, 0
};
// A scenery face's light, sent apart from its shape: it changes far more
// often (a new sun map several times a game hour, a light coming or going)
struct alignas(16) FaceLight {
  float sun0[4]{};  // its corners' shadow in sun map 0
  float sun1[4]{};  // and in map 1
  float local[4]{}; // the lamps' light rgb (0-1)
};
struct alignas(16) Shape {
  float ambient[4]{}; // sky colour rgb (0-255)
  float bounds[4]{};  // its sprite's place on the screen: x, y, w, h (logical)
  float uv[4]{};      // its mask in the atlas: u, v, w, h
  uint32_t flags[4]{}; // has a mask, mask required, mirrored, 0
};
// One face to draw: face (top bit: from actorFaces, else from setFaces'),
// and which of its corners (bits 0-3) ask the sun maps' shadow now
// (GpuSunD3D11, sent before drawing) instead of taking screen's z and w
struct Draw {
  uint32_t face, live;
};
struct Frame {
  float originX = 0, originY = 0, scale = 1, lamps = 0;
  float sun[3]{};
  float direct = 0, reference = 1, shadow = 0, sunBlend = 0;
  int targetW = 1, targetH = 1;
};

// The pass can run here (Direct3D 11 feature level 11, the compiler)
bool available(SDL_Renderer *renderer);
// The scenery faces, kept on the graphics card until key changes
void setFaces(const std::vector<Face> &faces, uint64_t key);
uint64_t facesKey();
// The scenery faces' light (setFaces' order), kept until key changes
void setFaceLight(const std::vector<FaceLight> &light, uint64_t key);
uint64_t faceLightKey();
// Draws into the renderer's current target (the lightmap), blended by the
// mask texture's coverage, as SDL_RenderGeometry with that texture does
bool draw(SDL_Renderer *renderer, SDL_Texture *mask, const std::vector<Draw> &order, const std::vector<Face> &actorFaces,
          const std::vector<Shape> &shapes, const Frame &frame);

// The ground's shadow (DayNightLighting::drawShadows) the same way: its
// quarter-tile points and quads kept on the graphics card until key changes;
// once a frame, which points ask the sun maps now; a pass (the map's, each
// tank's) sends which quads it draws, into SDL's current target, view and clip
struct alignas(16) GroundPoint {
  float world[4]{};  // xyz, 0
  float normal[4]{}; // xyz, 0
  float screen[4]{}; // x, y (camera-independent), 0, 0
};
struct GroundFrame {
  float originX = 0, originY = 0, strength = 0, sunBlend = 0;
};
void setGround(const std::vector<GroundPoint> &points, const std::vector<std::array<uint32_t, 4>> &quads, uint64_t key);
uint64_t groundKey();
// Each point's shadow in sun map 0 and map 1, kept until key changes
void setGroundSun(const std::vector<std::array<float, 2>> &sun, uint64_t key);
uint64_t groundSunKey();
// (a bit a point, 32 to a word: its shadow asked of the sun maps now)
bool setGroundLive(const std::vector<uint32_t> &bits);
bool drawGround(SDL_Renderer *renderer, const std::vector<uint32_t> &quads, const GroundFrame &frame);

} // namespace GpuSurfacesD3D11

#endif // GPU_SURFACES_D3D11_HPP
