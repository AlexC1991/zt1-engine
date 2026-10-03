#ifndef UI_STATUS_IMAGE_HPP
#define UI_STATUS_IMAGE_HPP

#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "UiElement.hpp"

// The original's UIStatusImage: a rating bar (zoo, animal, guest status).
// It shows one of its images (red, yellow, green) cropped to the value as a
// percentage of its width; "transition" values pick the image (below the
// first: red, below the second: yellow, otherwise green).
class UiStatusImage : public UiElement {
public:
  UiStatusImage(IniReader *ini_reader, ResourceManager *resource_manager,
                std::string name);
  ~UiStatusImage();

  UiAction handleInputs(std::vector<Input> &inputs) override;
  void draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) override;

  // 0-100
  void setValue(int value) { this->value = value < 0 ? 0 : (value > 100 ? 100 : value); }

private:
  std::vector<std::string> image_paths;
  std::vector<SDL_Texture *> images;
  std::vector<int> transitions;
  int value = 0;
};

#endif // UI_STATUS_IMAGE_HPP
