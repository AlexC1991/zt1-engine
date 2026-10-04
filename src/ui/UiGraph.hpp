#ifndef UI_GRAPH_HPP
#define UI_GRAPH_HPP

#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "UiElement.hpp"

// ============================================================================
// UIGraph: the Zoo Status panel's graphs (rating, donations, profit,
// attendance over time)
// ============================================================================
// Its layout section documents its settings: graphType (l line, b bar,
// p point), NumYLabels, VertPaddingPixels / HorizPaddingPixels,
// XValLabelPadding, ForcedMinY / ForcedMaxY, MaxBarWidth / BarSpacing,
// yIsMoney, and the colours (LineColor, BoxColor, YLabelColorPos/Neg,
// XLabelColor, PointLabelColor, PointColor, AxisLineColor).
//
// Measured against the original: the Y labels are right-aligned against the
// Y axis, which sits just right of the widest label; the X axis sits above
// the X labels (the months) at the bottom of the box; a value's first point
// (or bar) is HorizPaddingPixels right of the Y axis; the lowest value is
// drawn VertPaddingPixels + 1 above the X axis and the highest
// VertPaddingPixels + 4 below the axis top. Points are a black dot ringed in
// the line colour, labelled with their value 17 px above (green when above
// zero, else red); bars are MaxBarWidth - BarSpacing wide, grey, outlined.
// ============================================================================
class UiGraph : public UiElement {
public:
  UiGraph(IniReader *ini_reader, ResourceManager *resource_manager,
          std::string name);
  ~UiGraph();

  struct Point {
    double value = 0;
    std::string label; // the X axis label (a month)
  };
  void setPoints(const std::vector<Point> &points) { this->points = points; }
  // Line or bar (the page's Line / Bar buttons)
  void setBars(bool bars) { this->bars = bars; }

  UiAction handleInputs(std::vector<Input> &inputs) override;
  void draw(SDL_Renderer *renderer, SDL_Rect *layout_rect) override;

private:
  std::vector<Point> points;
  bool bars = false;
  bool money = false;
  int font = 0, font_size = 0;
  int num_y_labels = 10;
  int vert_padding = 5, horiz_padding = 20, x_label_padding = 3;
  int max_bar_width = 50, bar_spacing = 10;
  bool has_min = false, has_max = false;
  double forced_min = 0, forced_max = 0;
  SDL_Color line_color = {255, 0, 0, 255}, box_color = {100, 100, 100, 255};
  SDL_Color y_pos_color = {0, 255, 0, 255}, y_neg_color = {255, 0, 0, 255};
  SDL_Color x_label_color = {0, 255, 0, 255};
  SDL_Color point_label_color = {0, 255, 0, 255};
  SDL_Color point_color = {0, 0, 0, 255}, axis_color = {0, 0, 0, 255};

  SDL_Color color(const std::string &key, SDL_Color fallback);
  std::string format(double value) const;
  // Draws text with its top-left (or, centred, its top-centre) at x, y
  void text(SDL_Renderer *renderer, const std::string &s, SDL_Color c, int x,
            int y, int align); // align: -1 left, 0 centre, 1 right
  int textWidth(SDL_Renderer *renderer, const std::string &s);
};

#endif // UI_GRAPH_HPP
