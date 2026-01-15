import os

def apply(src_dir, root_dir):
    updated = False
    
    # 1. HEADER
    h_path = os.path.join(src_dir, "FontManager.hpp")
    h_code = """#ifndef FONT_MANAGER_HPP
#define FONT_MANAGER_HPP
#include <unordered_map>
#include <string>
#include "SDL_ttf.h"

class FontManager {
public:
  FontManager();
  ~FontManager();
  SDL_Texture * getStringTexture(SDL_Renderer * renderer, const int font, const std::string &string, SDL_Color color);
private:
  std::unordered_map<int, TTF_Font *> fonts;
  struct CachedTexture { SDL_Texture* texture; int width; int height; };
  std::unordered_map<std::string, CachedTexture> texture_cache;
  void loadFont(const int font);
  void clearCache();
};
#endif"""
    
    if os.path.exists(h_path):
        with open(h_path, "r", encoding="utf-8") as f:
            if f.read().replace('\r\n', '\n').strip() != h_code.replace('\r\n', '\n').strip():
                with open(h_path, "w", encoding="utf-8") as f: f.write(h_code)
                print("    -> Updated FontManager.hpp")
                updated = True

    # 2. SOURCE
    c_path = os.path.join(src_dir, "FontManager.cpp")
    c_code = """#include "FontManager.hpp"
#include <SDL2/SDL.h>
#include "Utils.hpp"
#include <sstream>

FontManager::FontManager() {}
FontManager::~FontManager() {
  this->clearCache();
  for (auto f : this->fonts) if (f.second) TTF_CloseFont(f.second);
  TTF_Quit();
}
void FontManager::clearCache() {
    for (auto& entry : texture_cache) if (entry.second.texture) SDL_DestroyTexture(entry.second.texture);
    texture_cache.clear();
}
SDL_Texture * FontManager::getStringTexture(SDL_Renderer * renderer, const int font, const std::string &string, SDL_Color color) {
  this->loadFont(font);
  std::stringstream ss; ss << font << "_" << string << "_" << (int)color.r << "_" << (int)color.g << "_" << (int)color.b << "_" << (int)color.a;
  std::string key = ss.str();
  
  if (texture_cache.count(key)) return texture_cache[key].texture;

  SDL_Surface * surface = TTF_RenderUTF8_Blended(this->fonts[font], string.c_str(), color);
  if (!surface) return NULL;
  SDL_Texture * texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (texture) { CachedTexture ct; ct.texture = texture; texture_cache[key] = ct; }
  return texture;
}
void FontManager::loadFont(const int font) {
  if (this->fonts.contains(font)) return;
  int size = 14; std::string file = "Aileron-Regular.otf";
  switch (font) {
    case 7100: file = "Aileron-Black.otf"; size = 18; break;
    case 7108: file = "Aileron-Bold.otf"; size = 16; break;
    case 4736: case 14004: case 11520: case 11522: case 14000: file = "Aileron-Black.otf"; size = 12; break;
  }
  this->fonts[font] = TTF_OpenFont(Utils::fixPath("fonts/" + file).c_str(), size);
}"""

    if os.path.exists(c_path):
        with open(c_path, "r", encoding="utf-8") as f:
            if f.read().replace('\r\n', '\n').strip() != c_code.replace('\r\n', '\n').strip():
                with open(c_path, "w", encoding="utf-8") as f: f.write(c_code)
                print("    -> Updated FontManager.cpp")
                updated = True
                
    return updated