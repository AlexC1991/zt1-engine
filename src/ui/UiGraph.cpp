#include "UiGraph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "../RenderSettings.hpp"

UiGraph::UiGraph(IniReader *ini_reader, ResourceManager *resource_manager,
                 std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;
  this->id = ini_reader->getInt(name, "id", 0);
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->anchor = ini_reader->getInt(name, "anchor", 0);

  this->font = ini_reader->getInt(name, "font", 0);
  this->font_size = ini_reader->getInt(name, "fontsize", 0);
  this->bars = ini_reader->get(name, "graphtype") == "b";
  auto section = ini_reader->getSection(name);
  auto integer = [&](const char *key, int fallback) {
    auto it = section.find(key);
    return it == section.end() || it->second.empty() ? fallback
                                                     : std::atoi(it->second.c_str());
  };
  this->num_y_labels = std::max(2, integer("numylabels", 10));
  this->vert_padding = integer("vertpaddingpixels", 5);
  this->horiz_padding = integer("horizpaddingpixels", 20);
  this->x_label_padding = integer("xvallabelpadding", 3);
  this->max_bar_width = integer("maxbarwidth", 50);
  this->bar_spacing = integer("barspacing", 10);
  int forcedMin = integer("forcedminy", -1), forcedMax = integer("forcedmaxy", -1);
  this->has_min = forcedMin != -1;
  this->has_max = forcedMax != -1;
  this->forced_min = forcedMin;
  this->forced_max = forcedMax;
  this->money = section.count("yismoney") > 0;

  this->line_color = color("linecolor", this->line_color);
  this->box_color = color("boxcolor", this->box_color);
  this->y_pos_color = color("ylabelcolorpos", this->y_pos_color);
  this->y_neg_color = color("ylabelcolorneg", this->y_neg_color);
  this->x_label_color = color("xlabelcolor", this->x_label_color);
  this->point_label_color = color("pointlabelcolor", this->point_label_color);
  this->point_color = color("pointcolor", this->point_color);
  this->axis_color = color("axislinecolor", this->axis_color);
}

UiGraph::~UiGraph() {
  for (UiElement *child : this->children)
    delete child;
}

SDL_Color UiGraph::color(const std::string &key, SDL_Color fallback) {
  std::vector<std::string> v = this->ini_reader->getList(this->name, key);
  if (v.size() < 3)
    return fallback;
  return {(Uint8)std::atoi(v[0].c_str()), (Uint8)std::atoi(v[1].c_str()),
          (Uint8)std::atoi(v[2].c_str()), 255};
}

std::string UiGraph::format(double value) const {
  long v = static_cast<long>(std::floor(value));
  std::string digits = std::to_string(v < 0 ? -v : v);
  return std::string(v < 0 ? "-" : "") + (this->money ? "$" : "") + digits;
}

int UiGraph::textWidth(SDL_Renderer *renderer, const std::string &s) {
  SDL_Texture *t = this->resource_manager->getStringTexture(
      renderer, this->font, s, SDL_Color{255, 255, 255, 255}, this->font_size);
  int w = 0, h = 0;
  if (t)
    this->resource_manager->getTextSize(t, &w, &h);
  return w;
}

void UiGraph::text(SDL_Renderer *renderer, const std::string &s, SDL_Color c,
                   int x, int y, int align) {
  if (!RenderSettings::drawsUiText())
    return;
  SDL_Texture *t = this->resource_manager->getStringTexture(
      renderer, this->font, s, c, this->font_size);
  if (!t)
    return;
  int w = 0, h = 0;
  this->resource_manager->getTextSize(t, &w, &h);
  int left = align < 0 ? x : align == 0 ? x - w / 2 : x - w;
  SDL_Rect r = {left, y, w, h};
  SDL_RenderCopy(renderer, t, nullptr, &r);
}

UiAction UiGraph::handleInputs(std::vector<Input> &inputs) {
  (void)inputs;
  return UiAction::NONE;
}

void UiGraph::draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) {
  if (!renderer || !layout_rect)
    return;
  SDL_Rect rect = this->getRect(this->ini_reader->getSection(this->name), layout_rect);
  this->last_rect = rect;
  const bool art = RenderSettings::drawsUiArt();

  // The value range: forced, else the data's (an empty or flat graph
  // spans 0 to 10)
  double lo = 0, hi = 0;
  bool first = true;
  for (const Point &p : this->points) {
    lo = first ? p.value : std::min(lo, p.value);
    hi = first ? p.value : std::max(hi, p.value);
    first = false;
  }
  if (this->has_min)
    lo = this->forced_min;
  if (this->has_max)
    hi = this->forced_max;
  if (hi <= lo)
    hi = lo + 10;

  // The Y labels, and the Y axis just right of the widest
  int n = this->num_y_labels;
  std::vector<double> labelValues;
  int widest = 0;
  for (int i = 0; i < n; i++) {
    labelValues.push_back(lo + (hi - lo) * i / (n - 1));
    widest = std::max(widest, this->textWidth(renderer, this->format(labelValues.back())));
  }
  const int lineH = this->resource_manager->getFontLineHeight(this->font, this->font_size);
  // (Text is drawn 3 px above where its glyphs start: the font's ascent
  // over the digits' height)
  const int glyph = 3;
  const int axisX = rect.x + 10 + widest;
  const int axisTop = rect.y + this->vert_padding;
  const int xLabelGlyphs = rect.y + rect.h - lineH;
  const int xLabelTop = xLabelGlyphs - glyph;
  const int axisY = xLabelGlyphs - this->x_label_padding - 3;
  const int right = rect.x + rect.w - 1;
  const double yTop = axisTop + this->vert_padding + 4;
  const double yBottom = axisY - this->vert_padding - 1;
  auto valueY = [&](double v) {
    return static_cast<int>(std::lround(yBottom - (v - lo) / (hi - lo) * (yBottom - yTop)));
  };

  // The labels are a whole number of pixels apart, down from the top
  // (so the lowest isn't quite level with its value)
  const int step = static_cast<int>((yBottom - yTop) / (n - 1));
  for (int i = 0; i < n; i++) {
    double v = labelValues[n - 1 - i];
    int y = static_cast<int>(yTop) + i * step;
    this->text(renderer, this->format(v), v >= 0 ? this->y_pos_color : this->y_neg_color,
               axisX - 2, y - lineH / 2 - glyph, 1);
  }

  if (art) {
    SDL_SetRenderDrawColor(renderer, this->axis_color.r, this->axis_color.g,
                           this->axis_color.b, 255);
    SDL_RenderDrawLine(renderer, axisX, axisTop, axisX, axisY);
    SDL_RenderDrawLine(renderer, axisX, axisY, right, axisY);
  }

  // The points (or bars), spread across the width
  const int count = static_cast<int>(this->points.size());
  const int left = axisX + this->horiz_padding;
  const int barW = std::max(1, this->max_bar_width - this->bar_spacing);
  auto pointX = [&](int i) {
    if (this->bars)
      return left + i * this->max_bar_width + barW / 2;
    if (count <= 1)
      return left;
    return left + (right - this->horiz_padding - left) * i / (count - 1);
  };
  auto labelColor = [&](double v) {
    return v > 0 ? this->point_label_color : this->y_neg_color;
  };

  if (!this->bars && art && count > 1) {
    SDL_SetRenderDrawColor(renderer, this->line_color.r, this->line_color.g,
                           this->line_color.b, 255);
    for (int i = 1; i < count; i++)
      SDL_RenderDrawLine(renderer, pointX(i - 1), valueY(this->points[i - 1].value),
                         pointX(i), valueY(this->points[i].value));
  }

  for (int i = 0; i < count; i++) {
    const Point &p = this->points[i];
    int x = pointX(i), y = valueY(p.value);
    if (this->bars) {
      int base = static_cast<int>(std::lround(yBottom));
      SDL_Rect box = {x - barW / 2, std::min(y, base), barW, std::abs(base - y) + 1};
      if (art) {
        SDL_SetRenderDrawColor(renderer, this->box_color.r, this->box_color.g,
                               this->box_color.b, 255);
        SDL_RenderFillRect(renderer, &box);
        SDL_SetRenderDrawColor(renderer, this->axis_color.r, this->axis_color.g,
                               this->axis_color.b, 255);
        SDL_RenderDrawRect(renderer, &box);
      }
    } else if (art) {
      // A dot ringed in the line colour (7 px across, 5 inside)
      for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 3; dx++) {
          int d = dx * dx + dy * dy;
          if (d > 10)
            continue;
          SDL_Color c = d > 4 ? this->line_color : this->point_color;
          SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
          SDL_RenderDrawPoint(renderer, x + dx, y + dy);
        }
    }
    this->text(renderer, this->format(p.value), labelColor(p.value), x, y - 21, 0);
    if (!p.label.empty())
      this->text(renderer, p.label, this->x_label_color, x, xLabelTop, 0);
  }
}
