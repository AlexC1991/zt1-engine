#ifndef INPUT_HPP
#define INPUT_HPP
#include <SDL2/SDL.h>

enum class InputType {
  NONE,
  POSITIONED,
  BUTTON,
};

enum class InputEvent {
  NONE,
  LEFT_CLICK,
  RIGHT_CLICK,
  CURSOR_MOVE,
  MOUSE_MOVE,      // [PATCH] Alias for CURSOR_MOVE
  SCROLL_UP,       // [PATCH] Mouse wheel up
  SCROLL_DOWN,     // [PATCH] Mouse wheel down
  QUIT
};

// [PATCH] Updated Input struct with direct x,y access
typedef struct {
  InputType type;
  InputEvent event;
  SDL_Point position;
  int x;  // [PATCH] Direct x coordinate
  int y;  // [PATCH] Direct y coordinate
} Input;

#endif // INPUT_HPP
