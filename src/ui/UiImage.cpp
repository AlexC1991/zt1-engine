#include "UiImage.hpp"

#include "../CompassDirection.hpp"
#include "../Utils.hpp"

// =================================================================================
//  USER CONFIGURATION: PREVIEW IMAGE ADJUSTMENTS
// =================================================================================
// Tweak these numbers to move and stretch the map preview image.

// 1. TARGET SIZE (Updated to your perfect fit)
static int TARGET_WIDTH  = 370;  // Width of the box
static int TARGET_HEIGHT = 276;  // Height of the box

// 2. POSITION OFFSET (Updated to your perfect fit)
static int OFFSET_X      = 0;    // Positive moves RIGHT, Negative moves LEFT
static int OFFSET_Y      = 0;    // Positive moves DOWN,  Negative moves UP

// =================================================================================

static bool isPreviewId(int id) {
  return id == 50001 || id == 11501;
}

UiImage::UiImage(
  IniReader *ini_reader,
  ResourceManager *resource_manager,
  std::string name
) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->anchor = ini_reader->getInt(name, "anchor", 0);

  // --- FIX: LAYER ORDER ---
  // Previously we forced 'layer = 8', which made the map draw ON TOP of the menu borders.
  // We removed that override so it uses the layer from the .lyt file (usually 1).
  // If it still overlaps the borders, we can try forcing it to 0 here.
  // if (isPreviewId(this->id)) { this->layer = 0; } 

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
  if (this->image_path == path && !this->is_zt1_preview) return;

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

void UiImage::setZt1Image(
  const std::string &raw_path,
  const std::string &pal_path
) {
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

void UiImage::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (renderer == nullptr || layout_rect == nullptr) {
    return;
  }

  if (((this->image == nullptr && this->animation == nullptr &&
        !this->image_path.empty()) ||
       this->is_dynamic) &&
      (this->is_zt1_preview || !this->image_path.empty())) {
    this->is_dynamic = false;

    if (this->is_zt1_preview) {
      this->image = this->resource_manager->getZt1Texture(
        renderer,
        this->zt1_raw_path,
        this->zt1_pal_path
      );
      // Logging suppressed to keep console clean
    } else if (!this->image_path.empty()) {
      std::string ext =
        Utils::string_to_lower(Utils::getFileExtension(this->image_path));

      if (ext == "png" || ext == "bmp" || ext == "tga") {
        this->image =
          this->resource_manager->getTexture(renderer, this->image_path);
      } else if (ext.empty() || ext == "ani") {
        this->animation =
          this->resource_manager->getAnimation(this->image_path);
      } else {
        this->image =
          this->resource_manager->getTexture(renderer, this->image_path);
      }
    }
  }

  SDL_Rect dest_rect =
    this->getRect(this->ini_reader->getSection(this->name), layout_rect);

  // --- MANUAL OVERRIDE FOR PREVIEWS ---
  if (isPreviewId(this->id)) {
      // 1. Force the size
      dest_rect.w = TARGET_WIDTH;
      dest_rect.h = TARGET_HEIGHT;

      // 2. Apply the offsets
      dest_rect.x += OFFSET_X;
      dest_rect.y += OFFSET_Y;
  }
  else {
      // Standard sizing for non-preview images
      if (this->image != nullptr) {
          if (dest_rect.w == 0 || dest_rect.h == 0) {
              int tw, th;
              SDL_QueryTexture(this->image, nullptr, nullptr, &tw, &th);
              dest_rect.w = tw;
              dest_rect.h = th;
          }
      }
  }

  if (this->image != nullptr) {
    SDL_SetTextureBlendMode(this->image, SDL_BLENDMODE_BLEND);

    if (SDL_RenderCopy(renderer, this->image, nullptr, &dest_rect) != 0) {
      SDL_Log(
        "SDL_RenderCopy failed for UiImage(id=%d name=%s): %s",
        this->id,
        this->name.c_str(),
        SDL_GetError()
      );
    }
  } else if (this->animation != nullptr) {
    this->animation->draw(renderer, &dest_rect, CompassDirection::N);
  }

  this->drawChildren(renderer, &dest_rect);
}