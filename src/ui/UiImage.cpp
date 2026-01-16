#include "UiImage.hpp"

#include "../CompassDirection.hpp"
#include "../Utils.hpp"

// TWEAK THESE:
// These are the max size of the map preview area inside the UI frame.
static int PREVIEW_MAX_W = 372;
static int PREVIEW_MAX_H = 272;

static bool isPreviewId(int id) {
  return id == 50001 || id == 11501;
}

static SDL_Rect fitAspectIntoBox(int src_w, int src_h, SDL_Rect box) {
  if (src_w <= 0 || src_h <= 0) return box;
  if (box.w <= 0 || box.h <= 0) return box;

  // scale = min(box.w/src_w, box.h/src_h)
  double sx = (double)box.w / (double)src_w;
  double sy = (double)box.h / (double)src_h;
  double s = (sx < sy) ? sx : sy;

  int out_w = (int)(src_w * s);
  int out_h = (int)(src_h * s);
  if (out_w < 1) out_w = 1;
  if (out_h < 1) out_h = 1;

  SDL_Rect out = box;
  out.w = out_w;
  out.h = out_h;

  // center inside box
  out.x = box.x + (box.w - out_w) / 2;
  out.y = box.y + (box.h - out_h) / 2;

  return out;
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

  // keep preview on top
  if (isPreviewId(this->id)) {
    this->layer = 8;
  }

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

      if (this->image != nullptr) {
        int tw = 0, th = 0;
        SDL_QueryTexture(this->image, nullptr, nullptr, &tw, &th);
        SDL_Log(
          "UiImage(id=%d name=%s): ZT1 preview loaded %dx%d from raw=%s pal=%s",
          this->id,
          this->name.c_str(),
          tw,
          th,
          this->zt1_raw_path.c_str(),
          this->zt1_pal_path.c_str()
        );
      } else {
        SDL_Log(
          "UiImage(id=%d name=%s): ZT1 preview load FAILED raw=%s pal=%s",
          this->id,
          this->name.c_str(),
          this->zt1_raw_path.c_str(),
          this->zt1_pal_path.c_str()
        );
      }
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

  // For preview images, define an explicit "box" even if dx/dy is missing.
  SDL_Rect preview_box = dest_rect;
  if (isPreviewId(this->id)) {
    preview_box.w = PREVIEW_MAX_W;
    preview_box.h = PREVIEW_MAX_H;
  }

  // Determine final draw rect:
  // - If layout gives dx/dy, use it.
  // - If preview, use preview_box with aspect-fit.
  // - Otherwise, fall back to texture size.
  SDL_Rect draw_rect = dest_rect;

  if (this->image != nullptr) {
    int tw = 0, th = 0;
    SDL_QueryTexture(this->image, nullptr, nullptr, &tw, &th);

    if (isPreviewId(this->id)) {
      draw_rect = fitAspectIntoBox(tw, th, preview_box);
    } else if (draw_rect.w == 0 || draw_rect.h == 0) {
      draw_rect.w = tw;
      draw_rect.h = th;
    }
  } else if (this->animation != nullptr) {
    if (draw_rect.w == 0 || draw_rect.h == 0) {
      this->animation->queryTexture(
        CompassDirection::N,
        &draw_rect.w,
        &draw_rect.h
      );
    }
  }

  // DEBUG: show preview box + draw rect
  if (isPreviewId(this->id)) {
    SDL_Log(
      "UiImage PREVIEW BOX id=%d x=%d y=%d w=%d h=%d | draw x=%d y=%d w=%d h=%d",
      this->id,
      preview_box.x,
      preview_box.y,
      preview_box.w,
      preview_box.h,
      draw_rect.x,
      draw_rect.y,
      draw_rect.w,
      draw_rect.h
    );

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(renderer, 0, 255, 255, 60);
    SDL_RenderFillRect(renderer, &preview_box);
    SDL_SetRenderDrawColor(renderer, 0, 255, 255, 200);
    SDL_RenderDrawRect(renderer, &preview_box);

    SDL_SetRenderDrawColor(renderer, 255, 0, 255, 80);
    SDL_RenderFillRect(renderer, &draw_rect);
    SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
    SDL_RenderDrawRect(renderer, &draw_rect);
  }

  if (this->image != nullptr) {
    SDL_SetTextureBlendMode(this->image, SDL_BLENDMODE_BLEND);

    if (SDL_RenderCopy(renderer, this->image, nullptr, &draw_rect) != 0) {
      SDL_Log(
        "SDL_RenderCopy failed for UiImage(id=%d name=%s): %s",
        this->id,
        this->name.c_str(),
        SDL_GetError()
      );
    }
  } else if (this->animation != nullptr) {
    this->animation->draw(renderer, &draw_rect, CompassDirection::N);
  }

  this->drawChildren(renderer, &draw_rect);
}