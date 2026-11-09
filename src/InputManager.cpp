#include <cmath>
#include "InputManager.hpp"

#include "Features.hpp"
#include "Window.hpp"

extern float g_ZoomLevel;
extern bool g_showDiagnostics;

InputManager::InputManager() {
}

InputManager::~InputManager() {
}

std::vector<Input> InputManager::getInputs() {
  std::vector<Input> inputs;
  SDL_Event event;
  
  while (SDL_PollEvent(&event)) {
    Input input = {
      .type = InputType::NONE,
      .event = InputEvent::NONE,
      .position = {0, 0},
      .x = 0,
      .y = 0,
      .key = 0,
      .text = {0}
    };
    
    switch (event.type) {
      case SDL_EVENT_QUIT:
        input.type = InputType::BUTTON;
        input.event = InputEvent::QUIT;
        break;
        
      case SDL_EVENT_KEY_DOWN:
        input.type = InputType::BUTTON;
        if (event.key.key == SDLK_ESCAPE) {
          input.event = InputEvent::KEY_ESCAPE;
        } else if (event.key.key == SDLK_F3 && !event.key.repeat) {
          // Toggle the diagnostics overlay
          g_showDiagnostics = !g_showDiagnostics;
          input.event = InputEvent::NONE;
        } else if (Features::hotkeys && !event.key.repeat &&
                   (event.key.key == SDLK_F11 ||
                    (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT)))) {
          // (the hotkeys extra) Alt+Enter or F11: full screen on or off, as
          // Windows games do (the original: the Game Options' button only)
          if (SDL_Window *w = SDL_GetWindowFromID(event.key.windowID))
            Window::setFullscreen(w, !(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN));
          input.event = InputEvent::NONE;
        } else {
          input.event = InputEvent::KEY_DOWN;
          input.key = event.key.key;
        }
        break;

      // Moved, resized, maximised or back: the player's window, kept (SDL3's
      // window events are events of their own)
      case SDL_EVENT_WINDOW_MOVED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      case SDL_EVENT_WINDOW_MAXIMIZED:
      case SDL_EVENT_WINDOW_RESTORED:
        Window::windowChanged(SDL_GetWindowFromID(event.window.windowID));
        break;

      case SDL_EVENT_TEXT_INPUT:
        input.type = InputType::BUTTON;
        input.event = InputEvent::TEXT_INPUT;
        SDL_strlcpy(input.text, event.text.text, sizeof(input.text));
        break;

      case SDL_EVENT_MOUSE_BUTTON_UP:
        input.type = InputType::POSITIONED;
        // Where it happened (the mouse may have moved on since: a press
        // and a drag can arrive in the same frame)
        // (SDL3's are fractional: the whole pixel, as SDL2 gave)
        input.position = {static_cast<int>(std::floor(event.button.x)), static_cast<int>(std::floor(event.button.y))};
        input.x = input.position.x;
        input.y = input.position.y;
        input.event = event.button.button == SDL_BUTTON_LEFT    ? InputEvent::LEFT_RELEASE
                      : event.button.button == SDL_BUTTON_RIGHT ? InputEvent::RIGHT_RELEASE
                                                                : InputEvent::NONE;
        break;
        
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        input.type = InputType::POSITIONED;
        input.position = {static_cast<int>(std::floor(event.button.x)), static_cast<int>(std::floor(event.button.y))};
        input.x = input.position.x;
        input.y = input.position.y;
        input.event = getEventFromMouseButton(event.button.button);
        break;
        
      case SDL_EVENT_MOUSE_MOTION:
        input.type = InputType::POSITIONED;
        input.event = InputEvent::CURSOR_MOVE;
        input.position = {static_cast<int>(std::floor(event.motion.x)), static_cast<int>(std::floor(event.motion.y))};
        input.x = input.position.x;
        input.y = input.position.y;
        break;
        
      // [PATCH] Handle mouse wheel scrolling
      case SDL_EVENT_MOUSE_WHEEL:
        input.type = InputType::POSITIONED;
        input.position = {static_cast<int>(std::floor(event.wheel.mouse_x)), static_cast<int>(std::floor(event.wheel.mouse_y))};
        input.x = input.position.x;
        input.y = input.position.y;
        
        // What the wheel does is up to the game: scroll the list under the
        // cursor, or zoom the map (see hudInputs)
        if (event.wheel.y > 0) {
          input.event = InputEvent::SCROLL_UP;
        } else if (event.wheel.y < 0) {
          input.event = InputEvent::SCROLL_DOWN;
        }
        break;
    }
    
    if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      MouseEvents::x = input.position.x;
      MouseEvents::y = input.position.y;
    }
    if (input.type != InputType::NONE && input.event != InputEvent::NONE) {
        inputs.push_back(input);
    }
  }
  
  return inputs;
}

InputEvent InputManager::getEventFromMouseButton(Uint8 button) {
    InputEvent event;
    switch (button) {
        case SDL_BUTTON_LEFT:
            event = InputEvent::LEFT_CLICK;
            break;
        case SDL_BUTTON_RIGHT:
            event = InputEvent::RIGHT_CLICK;
            break;
        case SDL_BUTTON_MIDDLE:
            event = InputEvent::NONE;
            break;
        default:
            event = InputEvent::NONE;
            break;
    }
    return event;
}
