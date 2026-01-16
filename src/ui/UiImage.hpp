#ifndef UI_IMAGE_HPP
#define UI_IMAGE_HPP

#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "UiElement.hpp"
#include "../Animation.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"

class UiImage : public UiElement {
public:
  UiImage(IniReader *ini_reader, ResourceManager *resource_manager, std::string name);
  ~UiImage();

  UiAction handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer *renderer, SDL_Rect *layout_rect);

  void setImage(const std::string &path);

  // NEW: ZT1 raw preview (N + pal)
  void setZt1Image(const std::string &raw_path, const std::string &pal_path);

private:
  std::string image_path = "";
  SDL_Texture *image = nullptr;
  Animation *animation = nullptr;
  bool is_dynamic = false;

  bool is_zt1_preview = false;
  std::string zt1_raw_path = "";
  std::string zt1_pal_path = "";
};

#endif // UI_IMAGE_HPP