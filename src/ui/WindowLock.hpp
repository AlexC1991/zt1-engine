#ifndef WINDOW_LOCK_HPP
#define WINDOW_LOCK_HPP

#include <SDL2/SDL.h>

class ResourceManager;

// The little gold lock beside a window's close button (ours, with the
// dragwindows extra): locked, the window can't be dragged. Its art is made
// from the game's own close button (ui/sharedui/close: the gold frame and
// its normal, lit and pressed looks), the X taken out and a padlock drawn in
// the X's colour - open or shut. The game ships no small lock of its own
// (its only one, ui/scenario/lock, is the 369 x 276 "locked" picture).
namespace WindowLock {

// Where the lock goes for a window whose close button is at closeRect: just
// to its left, the same size
SDL_Rect beside(const SDL_Rect &closeRect);

// Draws it as a toggle button: shut or open padlock; lit under the mouse;
// the gold rim while held down, and while locked
void draw(SDL_Renderer *renderer, ResourceManager *rm, const SDL_Rect &at, bool locked, bool lit, bool pressed);

} // namespace WindowLock

#endif // WINDOW_LOCK_HPP
