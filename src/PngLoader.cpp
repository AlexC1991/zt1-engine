#include "PngLoader.hpp"
#include "Utils.hpp"
#include <SDL2/SDL.h>
#include <SDL_image.h>


Animation *PngLoader::loadPngAsAnimation(const std::string &path) {
  if (path.empty()) {
    SDL_Log("[PngLoader] Error: Empty path provided");
    return nullptr;
  }

  // Use SDL_image to load the file
  SDL_Surface *surface = IMG_Load(path.c_str());
  if (!surface) {
    SDL_Log("[PngLoader] Failed to load image '%s': %s", path.c_str(),
            IMG_GetError());
    return nullptr;
  }

  SDL_Log("[PngLoader] Successfully loaded '%s' (%dx%d)", path.c_str(),
          surface->w, surface->h);

  // Create Animation using the new constructor
  Animation *anim = new Animation(surface);

  // Animation makes a copy, so we free our loaded surface
  SDL_FreeSurface(surface);

  return anim;
}
