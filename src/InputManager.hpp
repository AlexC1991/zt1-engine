#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <vector>

#include "Input.hpp"

// Where the mouse is, in window pixels, as its own events last said (a
// move, a press, a release) - SDL_GetMouseState follows the real mouse only,
// and a drag made of events (the Lab's Play: drag) never moves it
namespace MouseEvents {
inline int x = 0, y = 0;
}

class InputManager {
public:
  InputManager();
  ~InputManager();

  std::vector<Input> getInputs();

private:
  InputEvent getEventFromMouseButton(Uint8 button);
};

#endif // INPUT_MANAGER_HPP