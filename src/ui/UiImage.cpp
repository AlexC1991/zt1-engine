#include "UiImage.hpp"
#include "../RenderSettings.hpp"

#include <algorithm>

#include "../CompassDirection.hpp"
#include "../Utils.hpp"

// =================================================================================
//  USER CONFIGURATION: PREVIEW IMAGE ADJUSTMENTS
// =================================================================================
// Tweak these numbers to move and stretch the map preview image.

// 1. TARGET SIZE for map previews
static int TARGET_WIDTH = 370;  // Width of the preview box
static int TARGET_HEIGHT = 276; // Height of the preview box

// 2. POSITION OFFSET for map previews
static int OFFSET_X = 0; // Positive moves RIGHT, Negative moves LEFT
static int OFFSET_Y = 0; // Positive moves DOWN,  Negative moves UP

// =================================================================================
//  LOCKED CRATE IMAGE ADJUSTMENTS
// =================================================================================
// Tweak these numbers to adjust the locked crate image position and size.

// 3. LOCK IMAGE SIZE (Set to 0 to use original texture size)
static int LOCK_WIDTH = 0;  // Width of lock image (0 = auto from texture)
static int LOCK_HEIGHT = 0; // Height of lock image (0 = auto from texture)

// 4. LOCK IMAGE POSITION OFFSET (relative to center of preview area)
static int LOCK_OFFSET_X = 0; // Positive moves RIGHT, Negative moves LEFT
static int LOCK_OFFSET_Y = 0; // Positive moves DOWN,  Negative moves UP

// =================================================================================

static bool isPreviewId(int id) { return id == 50001 || id == 11501; }

// Helper to detect if this is the locked crate asset
static bool isLockAsset(const std::string &path) {
  // Normalize to lowercase for comparison
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return lower.find("ui/scenario/lock") != std::string::npos;
}

UiImage::UiImage(IniReader *ini_reader, ResourceManager *resource_manager,
                 std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  // An image without a layer is at the bottom (the five that have none are
  // backgrounds: the Zoo Profit page's overlay must not cover its Line /
  // Bar buttons)
  this->layer = ini_reader->getInt(name, "layer", 0);
  this->anchor = ini_reader->getInt(name, "anchor", 0);

  // --- FIX: LAYER ORDER ---
  // Previously we forced 'layer = 8', which made the map draw ON TOP of the
  // menu borders. We removed that override so it uses the layer from the .lyt
  // file (usually 1). If it still overlaps the borders, we can try forcing it
  // to 0 here. if (isPreviewId(this->id)) { this->layer = 0; }

  this->filler_anchor1 = ini_reader->getInt(name, "anchor1", 0);
  this->filler_anchor2 = ini_reader->getInt(name, "anchor2", 0);

  std::string normal = ini_reader->get(name, "normal");
  if (!normal.empty()) {
    this->image_path = normal;
  } else {
    if (ini_reader->isList(name, "animation")) {
      std::vector<std::string> animation_list =
          ini_reader->getList(name, "animation");
      this->image_path = animation_list[animation_list.size() - 1];
    } else {
      this->image_path = ini_reader->get(name, "animation");
    }
  }
}

UiImage::~UiImage() {
  if (this->image != nullptr) {
    SDL_DestroyTexture(this->image);
    this->image = nullptr;
  }

  this->animation = nullptr;

  for (UiElement *child : this->children) {
    delete child;
  }
}

void UiImage::setImage(const std::string &path) {
  if (this->image_path == path && !this->is_zt1_preview)
    return;

  if (this->image != nullptr) {
    SDL_DestroyTexture(this->image);
    this->image = nullptr;
  }

  this->animation = nullptr;

  this->is_zt1_preview = false;
  this->zt1_raw_path.clear();
  this->zt1_pal_path.clear();

  this->image_path = path;
  this->is_dynamic = true;
}

void UiImage::setZt1Image(const std::string &raw_path,
                          const std::string &pal_path) {
  if (this->is_zt1_preview && this->zt1_raw_path == raw_path &&
      this->zt1_pal_path == pal_path) {
    return;
  }

  if (this->image != nullptr) {
    SDL_DestroyTexture(this->image);
    this->image = nullptr;
  }

  this->animation = nullptr;

  this->is_zt1_preview = true;
  this->zt1_raw_path = raw_path;
  this->zt1_pal_path = pal_path;

  this->image_path.clear();
  this->is_dynamic = true;
}

UiAction UiImage::handleInputs(std::vector<Input> &inputs) {
  return handleInputChildren(inputs);
}

void UiImage::ensureLoaded(SDL_Renderer *renderer) {
  if (renderer == nullptr)
    return;

  if (((this->image == nullptr && this->animation == nullptr &&
        !this->image_path.empty()) ||
       this->is_dynamic) &&
      (this->is_zt1_preview || !this->image_path.empty())) {
    this->is_dynamic = false;

    if (this->is_zt1_preview) {
      this->image = this->resource_manager->getZt1Texture(
          renderer, this->zt1_raw_path, this->zt1_pal_path);
      // Logging suppressed to keep console clean
    } else if (!this->image_path.empty()) {
      std::string ext =
          Utils::string_to_lower(Utils::getFileExtension(this->image_path));

      if (ext == "png" || ext == "bmp" || ext == "tga") {
        this->image =
            this->resource_manager->getTexture(renderer, this->image_path);
        // Debug: log background loading
        if (this->name == "Background" || this->name == "background") {
          SDL_Log("UiImage DEBUG: name='%s' path='%s' image=%p",
                  this->name.c_str(), this->image_path.c_str(),
                  (void *)this->image);
        }
      } else if (ext.empty() || ext == "ani") {
        this->animation =
            this->resource_manager->getAnimation(this->image_path);
      } else {
        this->image =
            this->resource_manager->getTexture(renderer, this->image_path);
      }
    }
  }

}

bool UiImage::naturalSize(int *w, int *h) {
  *w = *h = 0;
  if (this->image)
    ArtScaler::querySize(this->image, w, h);
  else if (this->animation)
    this->animation->queryTexture(CompassDirection::N, w, h);
  return *w > 0 && *h > 0;
}

SDL_Rect UiImage::computeRect(SDL_Renderer *renderer, SDL_Rect *parent_rect) {
  ensureLoaded(renderer);
  auto section = this->ini_reader->getSection(this->name);
  int w, h;
  if (naturalSize(&w, &h)) {
    // Without an explicit size, an image is as big as its art (needed for
    // x=right / y=bottom placement)
    if (!section.contains("dx") || section["dx"] == "whole")
      if (!section.contains("dynamicwidth"))
        section["dx"] = std::to_string(w);
    if (!section.contains("dy") || section["dy"] == "whole" ||
        section["dy"] == "fitfont")
      if (!section.contains("dynamicheight"))
        section["dy"] = std::to_string(h);
  }
  return this->getRect(section, parent_rect);
}

void UiImage::drawTiled(SDL_Renderer *renderer, const SDL_Rect &area) {
  int w, h;
  if (!naturalSize(&w, &h) || area.w <= 0 || area.h <= 0)
    return;
  // No clip rect here: at a fractional UI scale SDL truncates the clip rect
  // to whole pixels, leaving a 1px seam where the filler meets the next
  // piece. Tiles that would overrun the area are cropped instead.
  for (int y = area.y; y < area.y + area.h; y += h) {
    for (int x = area.x; x < area.x + area.w; x += w) {
      int cw = std::min(w, area.x + area.w - x);
      int ch = std::min(h, area.y + area.h - y);
      SDL_Rect r = {x, y, cw, ch};
      if (this->image) {
        SDL_Rect src = ArtScaler::toTexture(this->image, {0, 0, cw, ch});
        RenderSettings::applyArtScaleMode(this->image);
        SDL_RenderCopy(renderer, this->image, &src, &r);
      } else if (cw == w && ch == h) {
        this->animation->draw(renderer, &r, CompassDirection::N);
      } else {
        SDL_Rect clip = r; // a partial animation tile (rare)
        SDL_RenderSetClipRect(renderer, &clip);
        SDL_Rect full = {x, y, w, h};
        this->animation->draw(renderer, &full, CompassDirection::N);
        SDL_RenderSetClipRect(renderer, nullptr);
      }
    }
  }
}

void UiImage::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (renderer == nullptr || layout_rect == nullptr) {
    return;
  }
  ensureLoaded(renderer);

  // Filler: repeat the art in the gap between its two anchor elements
  if (filler_a && filler_b) {
    SDL_Rect a = filler_a->computeRect(renderer, layout_rect);
    SDL_Rect b = filler_b->computeRect(renderer, layout_rect);
    int w, h;
    if (naturalSize(&w, &h)) {
      SDL_Rect area;
      if (this->ini_reader->getInt(this->name, "dynamicheight", 0)) {
        area = {a.x, a.y + a.h, w, b.y - (a.y + a.h)};
      } else {
        // Horizontal strip along the bottom edge (y=bottom) or top
        bool bottom = this->ini_reader->get(this->name, "y") == "bottom";
        int y = bottom ? layout_rect->y + layout_rect->h - h : layout_rect->y;
        area = {a.x + a.w, y, b.x - (a.x + a.w), h};
      }
      drawTiled(renderer, area);
      this->last_rect = area;
    }
    return;
  }

  SDL_Rect dest_rect = isPreviewId(this->id)
                           ? this->getRect(this->ini_reader->getSection(this->name),
                                           layout_rect)
                           : this->computeRect(renderer, layout_rect);

  // --- MANUAL OVERRIDE FOR PREVIEWS ---
  if (isPreviewId(this->id)) {
    // Check if this is the lock crate asset - center it instead of stretching
    if (this->is_zt1_preview && isLockAsset(this->zt1_raw_path) &&
        this->image != nullptr) {
      // Get the original texture dimensions
      int texW, texH;
      ArtScaler::querySize(this->image, &texW, &texH);

      // Use specified dimensions or fall back to texture dimensions
      int finalW = (LOCK_WIDTH > 0) ? LOCK_WIDTH : texW;
      int finalH = (LOCK_HEIGHT > 0) ? LOCK_HEIGHT : texH;

      // Center the lock image within the preview area
      dest_rect.x += (TARGET_WIDTH - finalW) / 2;
      dest_rect.y += (TARGET_HEIGHT - finalH) / 2;
      dest_rect.w = finalW;
      dest_rect.h = finalH;

      // Apply lock-specific offsets
      dest_rect.x += LOCK_OFFSET_X;
      dest_rect.y += LOCK_OFFSET_Y;
    } else {
      // Standard preview: stretch to fill
      dest_rect.w = TARGET_WIDTH;
      dest_rect.h = TARGET_HEIGHT;

      // Apply the offsets
      dest_rect.x += OFFSET_X;
      dest_rect.y += OFFSET_Y;
    }
  } else {
    // Standard sizing for non-preview images
    if (this->image != nullptr) {
      if (dest_rect.w == 0 || dest_rect.h == 0) {
        int tw, th;
        ArtScaler::querySize(this->image, &tw, &th);
        dest_rect.w = tw;
        dest_rect.h = th;
      }
    }
  }

  if (this->image != nullptr) {
    // Use BLENDMODE_NONE for background images to ignore alpha channel
    // (fixes 32-bit TGAs like aquascen.tga appearing black)
    if (this->name == "background" || this->name == "Background") {
      SDL_SetTextureBlendMode(this->image, SDL_BLENDMODE_NONE);
    } else {
      SDL_SetTextureBlendMode(this->image, SDL_BLENDMODE_BLEND);
    }

    RenderSettings::applyArtScaleMode(this->image);
    if (SDL_RenderCopy(renderer, this->image, nullptr, &dest_rect) != 0) {
      SDL_Log("SDL_RenderCopy failed for UiImage(id=%d name=%s): %s", this->id,
              this->name.c_str(), SDL_GetError());
    }
  } else if (this->animation != nullptr) {
    this->animation->draw(renderer, &dest_rect, CompassDirection::N);
  }

  this->last_rect = dest_rect;
  this->drawChildren(renderer, &dest_rect);
}