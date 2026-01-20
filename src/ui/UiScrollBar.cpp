#include "UiScrollBar.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"
#include <SDL2/SDL.h>

UiScrollBar::UiScrollBar(IniReader *ini_reader,
                         ResourceManager *resource_manager, std::string name) {
  this->name = name;
  this->id = ini_reader->getInt(name, "id", 0);
  this->x = ini_reader->getInt(name, "x", 0);
  this->y = ini_reader->getInt(name, "y", 0);
  this->w = ini_reader->getInt(name, "w", 20);        // Default width
  this->h = ini_reader->getInt(name, "h", 100);       // Default height
  this->layer = ini_reader->getInt(name, "layer", 0); // Correctly read layer

  // Attempt to read range or orientation if available
  // For now, assume vertical
}

UiScrollBar::~UiScrollBar() {}

UiAction UiScrollBar::handleInputs(std::vector<Input> &inputs) {
  // Basic interaction stub
  // If click on arrows or thumb, emit action?
  return UiAction::NONE;
}

void UiScrollBar::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (!renderer)
    return;

  this->dest_rect.x = layout_rect->x + this->x;
  this->dest_rect.y = layout_rect->y + this->y;
  this->dest_rect.w = this->w;
  this->dest_rect.h = this->h;

  // Draw Background
  SDL_SetRenderDrawColor(renderer, back_color.r, back_color.g, back_color.b,
                         back_color.a);
  SDL_RenderFillRect(renderer, &this->dest_rect);

  // Draw Thumb (Center placeholder)
  SDL_Rect thumbRect = this->dest_rect;
  thumbRect.h = this->thumb_size;
  thumbRect.y += (this->h - this->thumb_size) / 2; // Middle

  SDL_SetRenderDrawColor(renderer, thumb_color.r, thumb_color.g, thumb_color.b,
                         thumb_color.a);
  SDL_RenderFillRect(renderer, &thumbRect);

  // Border
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
  SDL_RenderDrawRect(renderer, &this->dest_rect);
}
