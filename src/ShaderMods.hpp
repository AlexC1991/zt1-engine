#ifndef SHADER_MODS_HPP
#define SHADER_MODS_HPP

#include <string>
#include <vector>

#include <SDL2/SDL.h>

// Shader mods (an addition): mods can bring their own shaders, as a game
// engine's materials and post effects. In a mod's folder:
//
//   shaders/<id>.json   {"name", "about", "stage": "art" | "screen",
//                        "shader": "<id>.frag", "on": true,
//                        "settings": [{"name", "label", "min", "max",
//                                      "value", "about"}]}
//   shaders/<id>.frag   GLSL 1.20 fragment shader
//
// art     runs once on each frame of the map's art - animals, people,
//         fences, objects - as it's made for the zoomed-in map, at 4x (an
//         HD art mod's frame when there is one). It reads
//           uniform sampler2D uColor;   the frame's colour (straight alpha)
//           uniform sampler2D uNormal;  its surface: rgb a normal (x right,
//                                       y up, z out of the screen; 0.5 is
//                                       0), a its height (0 at the outline,
//                                       1 its thickest)
//           uniform vec2 uSize;         the frame, in pixels
//           uniform float uKind;        0 animal or person, 1 fence (thin
//                                       lines), 2 object or building
//         and writes the frame's new colour (straight alpha). A pixel reads
//         its own place with texture2D(uColor, (floor(gl_FragCoord.xy) +
//         0.5) / uSize).
// screen  runs on the whole finished frame every frame: uColor is the
//         frame, uSize the window (uTime, seconds, for both).
// Each setting is a uniform float of its name. The shaders of the mods
// switched on are at work (Game Options > Shaders: each on or off, its
// settings as sliders), kept in zoo.ini [Shaders].
namespace Shaders {

struct Setting {
  std::string name, label, about;
  float min = 0, max = 1, value = 0, standard = 0;
};
struct Shader {
  std::string key;  // "<mod folder>/<id>"
  std::string name, about, stage, mod, file;
  bool on = true;
  std::vector<Setting> settings;
  // Its mod's own settings (mod.json "settings", Mods.hpp), also uniforms
  // by name (one of its own settings by the same name wins)
  std::vector<std::pair<std::string, float>> modValues;
  unsigned program = 0;
  bool failed = false;
  std::string log;
};

// The shaders read again (a mod switched on or off)
void refresh();
std::vector<Shader> &all();
// Whether an art shader is at work (the map's zoomed-in art made through it)
bool artActive();
// Counts each change (frames made before are made again)
int generation();
void setOn(const std::string &key, bool on);
void setValue(const std::string &key, const std::string &setting, float value);
// One of a mod's own settings changed: its shaders' uniforms follow
void setModValue(const std::string &mod, const std::string &setting, float value);

// The frame (RGBA32, the art's own size scaled up k times) through the art
// shaders at work: a new texture standing for art w x h, or nullptr
SDL_Texture *applyArt(SDL_Renderer *renderer, SDL_Surface *frame, int kind, int w, int h);
// The finished window frame through the screen shaders at work
void applyScreen(SDL_Renderer *renderer);

// A frame's surface (RGBA32): rgb its normal, a its height - from its
// outline (a rounded shape) and its shading (the detail). New, the caller
// frees it
SDL_Surface *normals(SDL_Surface *frame);

} // namespace Shaders

#endif // SHADER_MODS_HPP
