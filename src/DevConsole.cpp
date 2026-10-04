#include "DevConsole.hpp"

#include <sstream>

#include "ResourceManager.hpp"

void DevConsole::toggle() {
  this->open = !this->open;
  if (this->open) {
    SDL_StartTextInput();
    if (this->lines.empty())
      this->print("Dev console - type 'help' for commands. ` or F10 closes.");
  } else {
    SDL_StopTextInput();
  }
}

void DevConsole::print(const std::string &text) {
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line))
    this->lines.push_back(line);
  if (this->lines.size() > 200)
    this->lines.erase(this->lines.begin(), this->lines.begin() + (this->lines.size() - 200));
}

void DevConsole::submit() {
  std::string line = this->input;
  this->input.clear();
  this->historyAt = -1;
  if (line.empty())
    return;
  this->history.push_back(line);
  this->print("> " + line);
  std::vector<std::string> words;
  std::stringstream ss(line);
  std::string w;
  while (ss >> w)
    words.push_back(w);
  if (words.empty())
    return;
  if (words[0] == "clear") {
    this->lines.clear();
    return;
  }
  if (this->run) {
    std::string out = this->run(words);
    if (!out.empty())
      this->print(out);
  }
}

bool DevConsole::handleInputs(const std::vector<Input> &inputs) {
  // ` or F10 opens and closes it
  for (const Input &in : inputs)
    if (in.event == InputEvent::KEY_DOWN && (in.key == SDLK_BACKQUOTE || in.key == SDLK_F10)) {
      this->toggle();
      return true;
    }
  if (!this->open)
    return false;
  for (const Input &in : inputs) {
    if (in.event == InputEvent::TEXT_INPUT) {
      std::string t = in.text;
      if (t != "`")
        this->input += t;
    } else if (in.event == InputEvent::KEY_ESCAPE) {
      this->toggle();
      return true;
    } else if (in.event == InputEvent::KEY_DOWN) {
      if (in.key == SDLK_RETURN || in.key == SDLK_KP_ENTER) {
        this->submit();
      } else if (in.key == SDLK_BACKSPACE) {
        if (!this->input.empty())
          this->input.pop_back();
      } else if (in.key == SDLK_UP && !this->history.empty()) {
        if (this->historyAt < 0)
          this->historyAt = static_cast<int>(this->history.size());
        if (this->historyAt > 0)
          this->historyAt--;
        this->input = this->history[this->historyAt];
      } else if (in.key == SDLK_DOWN && this->historyAt >= 0) {
        this->historyAt++;
        if (this->historyAt >= static_cast<int>(this->history.size())) {
          this->historyAt = -1;
          this->input.clear();
        } else {
          this->input = this->history[this->historyAt];
        }
      }
    }
  }
  return true;
}

void DevConsole::draw(SDL_Renderer *renderer, ResourceManager *rm, int width, int height) {
  if (!this->open)
    return;
  const int lineH = 16, shown = 18;
  int boxH = std::min(height / 2, lineH * (shown + 1) + 12);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_Rect box = {0, 0, width, boxH};
  SDL_SetRenderDrawColor(renderer, 10, 16, 12, 215);
  SDL_RenderFillRect(renderer, &box);
  SDL_SetRenderDrawColor(renderer, 120, 200, 120, 255);
  SDL_RenderDrawLine(renderer, 0, boxH, width, boxH);
  rm->setTextScale(1.0f);
  auto text = [&](const std::string &s, int x, int y, SDL_Color c) {
    if (s.empty())
      return;
    if (SDL_Texture *t = rm->getStringTexture(renderer, 4136, s, c, 4137)) {
      int w = 0, h = 0;
      rm->getTextSize(t, &w, &h);
      SDL_Rect r = {x, y, w, h};
      SDL_RenderCopy(renderer, t, nullptr, &r);
    }
  };
  int rows = (boxH - 12) / lineH - 1;
  int first = std::max(0, static_cast<int>(this->lines.size()) - rows);
  int y = 4;
  for (size_t i = first; i < this->lines.size(); i++, y += lineH)
    text(this->lines[i], 8, y, SDL_Color{200, 230, 200, 255});
  bool caret = (SDL_GetTicks() / 500) % 2 == 0;
  text("> " + this->input + (caret ? "_" : ""), 8, boxH - lineH - 4, SDL_Color{255, 255, 160, 255});
}
