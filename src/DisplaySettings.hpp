#ifndef DISPLAY_SETTINGS_HPP
#define DISPLAY_SETTINGS_HPP

#include <functional>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

// The Game Options' Display, Graphics and Accessibility settings (additions:
// the original's only video settings were three fixed window sizes), kept in
// zoo.ini [user] and at work the moment they're picked:
//
//   windowmode    window | borderless | fullscreen  (fullscreen=0/1 too, the
//                 original's own key)
//   screenwidth / screenheight   the window's size (the original's keys)
//   fullscreenwidth / fullscreenheight   full screen's resolution
//   fpslimit      frames a second at most (0: no limit), 60
//   vsync         1: frames wait for the monitor (no tearing)
//   brightness    1 as drawn
//   artupscale    fsr | stylized | sharp | smooth | mmpx (mmpx: from the next start)
//   colorfilter   off | protanopia | deuteranopia | tritanopia, and
//   colorfilterstrength (0-1)
//   cursorsize    1, 2 or 3 (times the original's)
//   edgescroll    1: the window's edge scrolls the map, as the original
//
// In the Lab's Play view the Lab owns the window: its mode and size are left
// alone there.
namespace Display {

enum class Mode { Windowed, Borderless, Fullscreen };
// (Stylized: FSR's upscale with line art's look - GpuFsr::setStyled. A toon
// look is a shader mod's: ShaderMods.hpp)
enum class Art { Fsr, Sharp, Smooth, Mmpx, Stylized };
enum class Filter { Off, Protanopia, Deuteranopia, Tritanopia };

struct Size {
  int w = 0, h = 0;
  bool operator==(const Size &o) const { return w == o.w && h == o.h; }
};

// Reads them all and puts them to work (after the window and FSR are made)
void init(SDL_Window *window, SDL_Renderer *renderer);

Mode mode();
void setMode(Mode mode);
const char *modeName(Mode mode);

// The sizes the window's monitor offers (800x600 and up, biggest first);
// windowed, only those that fit its usable part (the taskbar left out)
std::vector<Size> resolutions();
// The size at work: the window's (windowed), full screen's resolution, or
// the monitor's (borderless)
Size resolution();
void setResolution(Size size);
// Whether the resolution can be picked now (not borderless, not in the Lab)
bool resolutionPickable();

int frameLimit(); // 0: none
void setFrameLimit(int fps);
// The frame's shortest time, ms (0: none)
float frameDelayMs();

bool vsync();
void setVsync(bool on);

float brightness();
void setBrightness(float value);

Art art();
void setArt(Art art);
// What the game started with (MMPX upscales the art as it loads: a change
// to or from it shows from the next start)
Art artAtStart();
const char *artName(Art art);
// The setting's words for it in zoo.ini
Art parseArt(const std::string &name);

Filter filter();
void setFilter(Filter filter);
float filterStrength();
void setFilterStrength(float strength);

int cursorSize();
void setCursorSize(int size);
// The game's cursors made again at the new size (main.cpp sets it)
extern std::function<void(int size)> onCursorSize;

bool edgeScroll();
void setEdgeScroll(bool on);

} // namespace Display

#endif // DISPLAY_SETTINGS_HPP
