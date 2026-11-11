#ifndef WINDOW_LOCK_HPP
#define WINDOW_LOCK_HPP

#include <SDL3/SDL.h>

class ResourceManager;
class UiElement;

// The little gold lock beside a window's close button (ours, with the
// dragwindows extra): locked, the window can't be dragged. Its art is made
// from the game's own close button (ui/sharedui/close: the gold frame and
// its normal, lit and pressed looks), the X taken out and a padlock drawn in
// the X's colour - open or shut. The game ships no small lock of its own
// (its only one, ui/scenario/lock, is the 369 x 276 "locked" picture).
namespace WindowLock {

// Where the lock goes for a window whose close button is at closeRect, the
// same size: just left of the title's letters (the texts drawn on the close
// button's row, under root); a title against the window's left edge
// (Exhibit/Show List), just right of it; no room either side, beside the
// close button. (Always beside the close button, it covered the end of long
// titles - Animal Information.)
SDL_Rect place(UiElement *root, const SDL_Rect &windowRect, const SDL_Rect &closeRect);

// Draws it as a toggle button: shut or open padlock; lit under the mouse;
// the gold rim while held down, and while locked
void draw(SDL_Renderer *renderer, ResourceManager *rm, const SDL_Rect &at, bool locked, bool lit, bool pressed);

} // namespace WindowLock

#endif // WINDOW_LOCK_HPP
