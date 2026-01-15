import os

def apply(src_dir, root_dir):
    """Add scroll wheel support and fix Input struct to include x,y coordinates"""
    
    modified = False
    
    # 1. Update Input.hpp
    input_hpp = os.path.join(src_dir, "Input.hpp")
    if os.path.exists(input_hpp):
        with open(input_hpp, "r", encoding="utf-8") as f:
            content = f.read()
        
        if "SCROLL_UP" not in content:
            new_input_hpp = '''#ifndef INPUT_HPP
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
'''
            with open(input_hpp, "w", encoding="utf-8") as f:
                f.write(new_input_hpp)
            print("    -> Updated Input.hpp with scroll events and x,y fields")
            modified = True
    
    # 2. Update InputManager.cpp
    input_cpp = os.path.join(src_dir, "InputManager.cpp")
    if os.path.exists(input_cpp):
        with open(input_cpp, "r", encoding="utf-8") as f:
            content = f.read()
        
        if "SDL_MOUSEWHEEL" not in content:
            new_input_cpp = '''#include "InputManager.hpp"

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
      .y = 0
    };
    
    switch (event.type) {
      case SDL_QUIT:
        input.type = InputType::BUTTON;
        input.event = InputEvent::QUIT;
        break;
        
      case SDL_KEYDOWN:
        input.type = InputType::BUTTON;
        input.event = InputEvent::NONE;
        break;
        
      case SDL_MOUSEBUTTONDOWN:
        input.type = InputType::POSITIONED;
        SDL_GetMouseState(&input.position.x, &input.position.y);
        input.x = input.position.x;
        input.y = input.position.y;
        input.event = getEventFromMouseButton(event.button.button);
        break;
        
      case SDL_MOUSEMOTION:
        input.type = InputType::POSITIONED;
        input.event = InputEvent::CURSOR_MOVE;
        SDL_GetMouseState(&input.position.x, &input.position.y);
        input.x = input.position.x;
        input.y = input.position.y;
        break;
        
      // [PATCH] Handle mouse wheel scrolling
      case SDL_MOUSEWHEEL:
        input.type = InputType::POSITIONED;
        SDL_GetMouseState(&input.position.x, &input.position.y);
        input.x = input.position.x;
        input.y = input.position.y;
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
'''
            with open(input_cpp, "w", encoding="utf-8") as f:
                f.write(new_input_cpp)
            print("    -> Updated InputManager.cpp with scroll wheel support")
            modified = True
    
    return modified
