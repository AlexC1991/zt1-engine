#ifndef DEV_CONSOLE_HPP
#define DEV_CONSOLE_HPP

#include <functional>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "Input.hpp"

class ResourceManager;

// The developer console (` or F10): typed commands that put the game into a
// state to watch (fake animal counts, dung, litter, worn fences, dirty
// water, money, time) - for testing what the staff and tanks do before the
// animals and guests exist. Not part of the original game.
class DevConsole {
public:
  bool isOpen() const { return this->open; }
  void toggle();
  // Takes the keyboard while open (true: the inputs were its)
  bool handleInputs(const std::vector<Input> &inputs);
  void draw(SDL_Renderer *renderer, ResourceManager *rm, int width, int height);
  void print(const std::string &line);
  void clear() { this->lines.clear(); }
  // Runs a command line; prints what it returns (lines split at '\n')
  std::function<std::string(const std::vector<std::string> &)> run;

private:
  bool open = false;
  std::string input;
  std::vector<std::string> lines;
  std::vector<std::string> history;
  int historyAt = -1;
  void submit();
};

#endif // DEV_CONSOLE_HPP
