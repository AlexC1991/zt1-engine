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
  const std::string &getImagePath() const { return this->image_path; }
  // A UIImageSet's images (its animation= lines), one shown at a time
  std::vector<std::string> getImageSet() const {
    return this->ini_reader->getList(this->name, "animation");
  }

  SDL_Rect computeRect(SDL_Renderer *renderer, SDL_Rect *parent_rect) override;

  // Fillers ("anchor1"/"anchor2" + dynamicwidth/dynamicheight) stretch
  // between two other elements by repeating their art
  int getFillerAnchor1() const { return filler_anchor1; }
  int getFillerAnchor2() const { return filler_anchor2; }
  void setFillerAnchors(UiElement *a, UiElement *b) {
    filler_a = a;
    filler_b = b;
  }

  // NEW: ZT1 raw preview (N + pal)
  void setZt1Image(const std::string &raw_path, const std::string &pal_path);

private:
  std::string image_path = "";
  SDL_Texture *image = nullptr;
  Animation *animation = nullptr;
  bool is_dynamic = false;

  int filler_anchor1 = 0, filler_anchor2 = 0;
  UiElement *filler_a = nullptr, *filler_b = nullptr;

  void ensureLoaded(SDL_Renderer *renderer);
  bool naturalSize(int *w, int *h);
  void drawTiled(SDL_Renderer *renderer, const SDL_Rect &area);

  bool is_zt1_preview = false;
  std::string zt1_raw_path = "";
  std::string zt1_pal_path = "";
};

#endif // UI_IMAGE_HPP