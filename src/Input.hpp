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
  KEY_ESCAPE,      // ESC key pressed
  QUIT,
  LEFT_RELEASE,    // mouse buttons let go (drags: fences, terrain)
  RIGHT_RELEASE,
  KEY_DOWN,        // any other key (Input::key is its SDL keycode)
  TEXT_INPUT,      // typed text (Input::text, UTF-8)
};

// [PATCH] Updated Input struct with direct x,y access
typedef struct {
  InputType type;
  InputEvent event;
  SDL_Point position;
  int x;  // [PATCH] Direct x coordinate
  int y;  // [PATCH] Direct y coordinate
  int key;       // KEY_DOWN: SDL keycode
  char text[32]; // TEXT_INPUT: what was typed
} Input;

#endif // INPUT_HPP
