#include "UiStatusImage.hpp"

#include <algorithm>
#include <cstdlib>

UiStatusImage::UiStatusImage(IniReader *ini_reader,
                             ResourceManager *resource_manager,
                             std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;
  this->id = ini_reader->getInt(name, "id", 0);
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->anchor = ini_reader->getInt(name, "anchor", 0);
  this->image_paths = ini_reader->getList(name, "image");
  for (const std::string &t : ini_reader->getList(name, "transition"))
    this->transitions.push_back(std::atoi(t.c_str()));
}

UiStatusImage::~UiStatusImage() {
  for (SDL_Texture *t : images)
    if (t)
      SDL_DestroyTexture(t);
  for (UiElement *child : this->children)
    delete child;
}

UiAction UiStatusImage::handleInputs(std::vector<Input> &inputs) {
  return handleInputChildren(inputs);
}

void UiStatusImage::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (!renderer || !layout_rect)
    return;
  if (images.empty()) {
    for (const std::string &path : image_paths)
      images.push_back(resource_manager->getTexture(renderer, path));
  }

  // Which image: before the first transition red, then yellow, then green
  size_t pick = 0;
  while (pick < transitions.size() && value >= transitions[pick])
    pick++;
  if (pick >= images.size())
    pick = images.empty() ? 0 : images.size() - 1;

  SDL_Rect rect = this->getRect(this->ini_reader->getSection(this->name), layout_rect);
  this->last_rect = rect;
  SDL_Texture *tex = pick < images.size() ? images[pick] : nullptr;
  if (tex && value > 0) {
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    int full = rect.w > 0 ? rect.w : tw;
    int w = full * value / 100;
    SDL_Rect src = {0, 0, std::min(w, tw), th};
    SDL_Rect dst = {rect.x, rect.y, w, th};
    SDL_RenderCopy(renderer, tex, &src, &dst);
  }
  this->drawChildren(renderer, &rect);
}
