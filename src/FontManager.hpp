#ifndef FONT_MANAGER_HPP
#define FONT_MANAGER_HPP
#include <functional>
#include <string>
#include <unordered_map>
#include "SDL_ttf.h"

// ============================================================================
// FONT MANAGER
// ============================================================================
// Layouts name fonts by string-table id, as the original does: "font=14002"
// is the face ("Arial Bold") and "fontsize=14003" the point size ("10").
// The original draws them with Windows GDI: 96 DPI, no anti-aliasing, and a
// one-pixel dark shadow down and to the right. This reproduces that.
//
// UI scaling: with a text scale above 1 (menus scaled to fit the window),
// text is rendered by the font at the final screen size, so it stays sharp,
// with smoothing on and the shadow scaled to match. Callers keep working in
// layout units: querySize() reports a texture's size in those units.
// ============================================================================

class FontManager {
public:
  using StringLookup = std::function<std::string(uint32_t)>;

  FontManager();
  ~FontManager();

  // Where face names and sizes come from (the game's string table)
  void setStringLookup(StringLookup lookup) { strings = lookup; }

  // fontSizeId 0 = the size id that conventionally follows the face id
  SDL_Texture *getStringTexture(SDL_Renderer *renderer, int fontId,
                                const std::string &string, SDL_Color color,
                                int fontSizeId = 0);

  // Height of one line of text in this font, in layout pixels
  int getLineHeight(int fontId, int fontSizeId = 0);

  // Screen pixels per layout pixel for text rendered from now on
  void setScale(float scale) { this->scale = scale > 0.0f ? scale : 1.0f; }

  // Size of a texture from getStringTexture, in layout pixels
  void querySize(SDL_Texture *texture, int *w, int *h);

private:
  StringLookup strings;
  float scale = 1.0f;
  std::unordered_map<std::string, TTF_Font *> fonts;
  std::unordered_map<std::string, SDL_Texture *> texture_cache;
  std::unordered_map<SDL_Texture *, float> texture_scale;

  TTF_Font *getFont(int fontId, int fontSizeId, float scale);
  void clearCache();
};
#endif
