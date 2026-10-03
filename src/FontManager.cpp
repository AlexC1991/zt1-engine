#include "FontManager.hpp"
#include <SDL2/SDL.h>
#include "Utils.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace {

// Windows font file for a GDI face name. Only Arial and Arial Bold appear in
// the game's layouts.
std::string systemFontFile(std::string face) {
  std::transform(face.begin(), face.end(), face.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  const char *file = nullptr;
  if (face == "arial bold")
    file = "arialbd.ttf";
  else if (face == "arial")
    file = "arial.ttf";
  else if (face == "arial italic")
    file = "ariali.ttf";
  else if (face == "arial bold italic")
    file = "arialbi.ttf";
  if (!file)
    return "";

  std::vector<std::string> dirs;
  if (const char *windir = std::getenv("WINDIR"))
    dirs.push_back(std::string(windir) + "/Fonts/");
  dirs.push_back("C:/Windows/Fonts/");
  dirs.push_back("/usr/share/fonts/truetype/msttcorefonts/");
  for (const std::string &dir : dirs) {
    std::string path = dir + file;
    if (std::filesystem::exists(path))
      return path;
  }
  return "";
}

bool isBold(const std::string &face) {
  return face.find("Bold") != std::string::npos ||
         face.find("bold") != std::string::npos;
}

} // namespace

FontManager::FontManager() {}

FontManager::~FontManager() {
  this->clearCache();
  for (auto &f : this->fonts)
    if (f.second)
      TTF_CloseFont(f.second);
  TTF_Quit();
}

void FontManager::clearCache() {
  for (auto &entry : texture_cache)
    if (entry.second)
      SDL_DestroyTexture(entry.second);
  texture_cache.clear();
}

TTF_Font *FontManager::getFont(int fontId, int fontSizeId, float scale) {
  // The same font at another UI scale is another TTF_Font (rendered at that
  // DPI), so the scale is part of the key
  int dpi = static_cast<int>(96.0f * scale + 0.5f);
  std::string key = std::to_string(fontId) + "_" + std::to_string(fontSizeId) +
                    "_" + std::to_string(dpi);
  auto it = fonts.find(key);
  if (it != fonts.end())
    return it->second;

  std::string face = strings ? strings(fontId) : "";
  int points = 0;
  if (strings) {
    // The size id is given by the layout, or follows the face id
    std::vector<int> sizeIds;
    if (fontSizeId > 0)
      sizeIds.push_back(fontSizeId);
    sizeIds.push_back(fontId + 1);
    sizeIds.push_back(fontId + 2);
    for (int id : sizeIds) {
      points = std::atoi(strings(id).c_str());
      if (points > 0)
        break;
    }
  }
  if (points <= 0)
    points = 10;

  TTF_Font *font = nullptr;
  std::string file = systemFontFile(face);
  if (!file.empty()) {
    font = TTF_OpenFontDPI(file.c_str(), points, dpi, dpi);
  }
  if (!font) {
    // Bundled fallback when the Windows font isn't available
    std::string fallback = isBold(face) ? "Aileron-Bold.otf" : "Aileron-Regular.otf";
    font = TTF_OpenFontDPI(Utils::fixPath("fonts/" + fallback).c_str(), points,
                           dpi, dpi);
    SDL_Log("[FontManager] '%s' not found, using %s", face.c_str(),
            fallback.c_str());
  }
  if (font) {
    // At 1x, GDI-style: hinted for monochrome (rendered without smoothing).
    // Scaled up, ordinary hinting with smoothing reads better.
    TTF_SetFontHinting(font, dpi == 96 ? TTF_HINTING_MONO : TTF_HINTING_NORMAL);
  }
  fonts[key] = font;
  return font;
}

int FontManager::getLineHeight(int fontId, int fontSizeId) {
  // Layout metric: always measured at 1x
  TTF_Font *font = getFont(fontId, fontSizeId, 1.0f);
  return font ? TTF_FontHeight(font) : 14;
}

void FontManager::querySize(SDL_Texture *texture, int *w, int *h) {
  int tw = 0, th = 0;
  if (texture)
    SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
  auto it = texture_scale.find(texture);
  float s = it != texture_scale.end() ? it->second : 1.0f;
  if (w)
    *w = static_cast<int>(tw / s + 0.5f);
  if (h)
    *h = static_cast<int>(th / s + 0.5f);
}

SDL_Texture *FontManager::getStringTexture(SDL_Renderer *renderer, int fontId,
                                           const std::string &string,
                                           SDL_Color color, int fontSizeId) {
  if (string.empty())
    return nullptr;
  TTF_Font *font = getFont(fontId, fontSizeId, scale);
  if (!font)
    return nullptr;
  const bool smooth = scale > 1.01f;

  std::stringstream ss;
  ss << fontId << "_" << fontSizeId << "_" << scale << "_" << (int)color.r << "_"
     << (int)color.g << "_" << (int)color.b << "_" << (int)color.a << "_"
     << string;
  std::string key = ss.str();
  auto cached = texture_cache.find(key);
  if (cached != texture_cache.end())
    return cached->second;

  auto render = [&](SDL_Color c) {
    return smooth ? TTF_RenderUTF8_Blended(font, string.c_str(), c)
                  : TTF_RenderUTF8_Solid(font, string.c_str(), c);
  };
  SDL_Surface *text = render(color);
  SDL_Surface *shadow = render(SDL_Color{0, 0, 0, 255});
  if (!text || !shadow) {
    if (text)
      SDL_FreeSurface(text);
    if (shadow)
      SDL_FreeSurface(shadow);
    return nullptr;
  }

  // Text over its shadow, offset one layout pixel down and to the right
  int offset = std::max(1, static_cast<int>(scale + 0.5f));
  SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(
      0, text->w + offset, text->h + offset, 32, SDL_PIXELFORMAT_ARGB8888);
  SDL_Texture *texture = nullptr;
  if (out) {
    SDL_FillRect(out, nullptr, SDL_MapRGBA(out->format, 0, 0, 0, 0));
    if (smooth) {
      // Blend the smoothed glyph edges rather than overwriting
      SDL_SetSurfaceBlendMode(shadow, SDL_BLENDMODE_BLEND);
      SDL_SetSurfaceBlendMode(text, SDL_BLENDMODE_BLEND);
    }
    SDL_Rect shadowPos = {offset, offset, 0, 0};
    SDL_BlitSurface(shadow, nullptr, out, &shadowPos);
    SDL_Rect textPos = {0, 0, 0, 0};
    SDL_BlitSurface(text, nullptr, out, &textPos);
    texture = SDL_CreateTextureFromSurface(renderer, out);
    SDL_FreeSurface(out);
  }
  SDL_FreeSurface(text);
  SDL_FreeSurface(shadow);

  if (texture) {
    // Drawn at the size it was rendered for, pixel for pixel
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    texture_cache[key] = texture;
    texture_scale[texture] = scale;
  }
  return texture;
}
