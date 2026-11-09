#ifndef GRAPHICS_EFFECTS_HPP
#define GRAPHICS_EFFECTS_HPP

#include <SDL3/SDL.h>

// addition: graphics effects for the map, as a modern game's graphics page
// offers them - each one off unless picked (off: the map as the original
// draws it), set on Game Options > Graphics, kept in zoo.ini [user]:
//   fxaa       0/1   anti-aliasing: the jagged edges where art meets art
//                    smoothed (FXAA: Timothy Lottes' fast approximate AA)
//   bloom      0-2   bright parts glowing softly into what's around them
//   tiltshift  0-2   the top and bottom of the view blurred: the miniature
//                    look of a model zoo seen close up
//   contrast   0-100 more contrast (0: none added)
//   vibrance   0-100 duller colours lifted most (0: none)
//   vignette   0/1   the view's corners a little darker
//   grain      0/1   a fine moving film grain
// They run on the map's picture only, as it's finished and before the
// game's windows and bars go over it, on the graphics card (GpuFsr's
// passes: Direct3D 11's HLSL or OpenGL's GLSL, whichever the game runs on).
namespace GraphicsEffects {

struct Settings {
  bool fxaa = false;
  int bloom = 0, tiltShift = 0;
  int contrast = 0, vibrance = 0;
  bool vignette = false, grain = false;
};
const Settings &settings();
void set(const Settings &s); // (kept in zoo.ini)
void load();                 // (from zoo.ini)
bool anyOn();

// The map's picture through the effects picked (after the map is drawn,
// before the windows and bars)
void applyMap(SDL_Renderer *renderer);

} // namespace GraphicsEffects

#endif // GRAPHICS_EFFECTS_HPP
