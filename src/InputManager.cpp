#include "InputManager.hpp"

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
      case SDL_QUIT:
        input.type = InputType::BUTTON;
        input.event = InputEvent::QUIT;
        break;
        
      case SDL_KEYDOWN:
        input.type = InputType::BUTTON;
        if (event.key.keysym.sym == SDLK_ESCAPE) {
          input.event = InputEvent::KEY_ESCAPE;
        } else if (event.key.keysym.sym == SDLK_F3 && !event.key.repeat) {
          // Toggle the diagnostics overlay
          g_showDiagnostics = !g_showDiagnostics;
          input.event = InputEvent::NONE;
        } else {
          input.event = InputEvent::KEY_DOWN;
          input.key = event.key.keysym.sym;
        }
        break;

      case SDL_TEXTINPUT:
        input.type = InputType::BUTTON;
        input.event = InputEvent::TEXT_INPUT;
        SDL_strlcpy(input.text, event.text.text, sizeof(input.text));
        break;

      case SDL_MOUSEBUTTONUP:
        input.type = InputType::POSITIONED;
        // Where it happened (the mouse may have moved on since: a press
        // and a drag can arrive in the same frame)
        input.position = {event.button.x, event.button.y};
        input.x = input.position.x;
        input.y = input.position.y;
        input.event = event.button.button == SDL_BUTTON_LEFT    ? InputEvent::LEFT_RELEASE
                      : event.button.button == SDL_BUTTON_RIGHT ? InputEvent::RIGHT_RELEASE
                                                                : InputEvent::NONE;
        break;
        
      case SDL_MOUSEBUTTONDOWN:
        input.type = InputType::POSITIONED;
        input.position = {event.button.x, event.button.y};
        input.x = input.position.x;
        input.y = input.position.y;
        input.event = getEventFromMouseButton(event.button.button);
        break;
        
      case SDL_MOUSEMOTION:
        input.type = InputType::POSITIONED;
        input.event = InputEvent::CURSOR_MOVE;
        input.position = {event.motion.x, event.motion.y};
        input.x = input.position.x;
        input.y = input.position.y;
        break;
        
      // [PATCH] Handle mouse wheel scrolling
      case SDL_MOUSEWHEEL:
        input.type = InputType::POSITIONED;
        SDL_GetMouseState(&input.position.x, &input.position.y);
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
