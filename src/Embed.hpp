#ifndef EMBED_HPP
#define EMBED_HPP

// The game inside another program's window (the ZT1 Lab app's Play view):
// zt1-engine.exe --embed <parent window handle>. The game's window becomes a
// child of that window; the parent says where it goes, and the game is
// told to come forward or go, as lines on stdin:
//   place <x> <y> <w> <h>   (device pixels, in the parent's client area;
//                            rect: the same on the screen)
//   show | hide | focus | quit
//   click <x> <y> | rclick <x> <y>   (a mouse click at the window's pixels)
//   move <x> <y>   (the mouse there, resting: hovering, tooltips)
//   press <x> <y> | release <x> <y>   (the left button down / up there:
//                      with moves between, a drag at the Lab's own pace)
//   snap   (the screen as drawn with its tooltip, tooltip.bmp)
//   shot <file.bmp>   (the next frame as drawn, saved there - the same line
//                      the original's hook answers, so the Lab's Play
//                      picture works for both games)
//   volume <0-100>   (its own volume, src/SessionVolume.hpp)
#include <SDL2/SDL.h>

#include <string>

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
// Where the next frame is to be saved (shot <file>): the path once, then ""
std::string shotAsked();
} // namespace Embed

#endif
