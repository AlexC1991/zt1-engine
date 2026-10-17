#ifndef EMBED_HPP
#define EMBED_HPP

// The game inside another program's window (the ZT1 Lab app's Play view):
// zt1-engine.exe --embed <parent window handle>. The game's window becomes a
// child of that window; the parent says where it goes, and the game is
// told to come forward or go, as lines on stdin:
//   rect <x> <y> <w> <h>   (device pixels, in the parent's client area)
//   show | hide | focus | quit
//   click <x> <y> | rclick <x> <y>   (a mouse click at the window's pixels)
//   move <x> <y>   (the mouse there, resting: hovering, tooltips)
//   snap   (the game's own screenshot of the screen as drawn, Screenshots/)
//   volume <0-100>   (its own volume, src/SessionVolume.hpp)
#include <SDL2/SDL.h>

namespace Embed {
// Makes the window a child of the parent (a handle as decimal or 0x hex);
// false if it can't
bool into(SDL_Window *window, const char *parentHandle);
// Whether --embed was given and worked
bool active();
// Asked to quit (the Lab app closing the game)
bool quitAsked();
// Embedded and not on show (the Lab on another view, or minimised): the
// game holds still - no updates, no sound - until it's shown again
bool hidden();
// Asked for a screenshot (snap): true once, then cleared
bool snapAsked();
} // namespace Embed

#endif
