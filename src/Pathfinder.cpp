#include "Pathfinder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>

namespace Pathfinder {

namespace {
constexpr float kDiagonal = 1.41421356f;

// Reused between searches: a search marks what it touched with its own
// generation, so nothing needs clearing
struct Scratch {
  int width = 0, height = 0;
  uint32_t generation = 0;
  std::vector<uint32_t> seen, closed;
  std::vector<float> cost;
  std::vector<int> from;
  void ready(int w, int h) {
    if (w != width || h != height) {
      width = w;
      height = h;
      size_t n = static_cast<size_t>(w) * h;
      seen.assign(n, 0);
      closed.assign(n, 0);
      cost.assign(n, 0);
      from.assign(n, -1);
      generation = 0;
    }
    if (++generation == 0) {
      std::fill(seen.begin(), seen.end(), 0);
      std::fill(closed.begin(), closed.end(), 0);
      generation = 1;
    }
  }
};
thread_local Scratch scratch;

float octile(int x, int y, int gx, int gy) {
  float dx = static_cast<float>(std::abs(x - gx)), dy = static_cast<float>(std::abs(y - gy));
  return std::max(dx, dy) + (kDiagonal - 1.0f) * std::min(dx, dy);
}
} // namespace

bool find(int width, int height, int sx, int sy, int gx, int gy, const Pass &pass,
          const Step &step, std::vector<std::pair<int, int>> &out, int maxNodes,
          const Cost &tileCost) {
  out.clear();
  if (sx < 0 || sy < 0 || gx < 0 || gy < 0 || sx >= width || sy >= height || gx >= width ||
      gy >= height)
    return false;
  if (sx == gx && sy == gy) {
    out.push_back({sx, sy});
    return true;
  }
  Scratch &s = scratch;
  s.ready(width, height);
  const uint32_t g = s.generation;
  struct Node {
    float f;
    int index;
    bool operator>(const Node &o) const { return f > o.f; }
  };
  std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
  int start = sy * width + sx, goal = gy * width + gx;
  s.seen[start] = g;
  s.cost[start] = 0;
  s.from[start] = -1;
  open.push({octile(sx, sy, gx, gy), start});
  auto ok = [&](int x, int y) {
    return (x == gx && y == gy) || pass(x, y);
  };
  int expanded = 0;
  while (!open.empty() && expanded < maxNodes) {
    Node n = open.top();
    open.pop();
    if (s.closed[n.index] == g)
      continue;
    s.closed[n.index] = g;
    expanded++;
    if (n.index == goal)
      break;
    int x = n.index % width, y = n.index / width;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        if (dx == 0 && dy == 0)
          continue;
        int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= width || ny >= height)
          continue;
        int ni = ny * width + nx;
        if (s.closed[ni] == g || !ok(nx, ny))
          continue;
        float c;
        if (dx != 0 && dy != 0) {
          // Both ways round the corner must be open
          if (!pass(nx, y) || !pass(x, ny) || !step(x, y, nx, y) || !step(nx, y, nx, ny) ||
              !step(x, y, x, ny) || !step(x, ny, nx, ny))
            continue;
          c = kDiagonal;
        } else {
          if (!step(x, y, nx, ny))
            continue;
          c = 1.0f;
        }
        if (tileCost)
          c *= tileCost(nx, ny);
        float total = s.cost[n.index] + c;
        if (s.seen[ni] == g && total >= s.cost[ni])
          continue;
        s.seen[ni] = g;
        s.cost[ni] = total;
        s.from[ni] = n.index;
        open.push({total + octile(nx, ny, gx, gy), ni});
      }
  }
  if (s.closed[goal] != g)
    return false;
  for (int at = goal; at >= 0; at = s.from[at]) {
    out.push_back({at % width, at / width});
    if (at == start)
      break;
  }
  std::reverse(out.begin(), out.end());
  return true;
}

bool findGraph(int nodes, int start, int goal, const Next &next, const Guess &guess,
               std::vector<int> &out, int maxNodes) {
  out.clear();
  if (start < 0 || goal < 0 || start >= nodes || goal >= nodes)
    return false;
  std::vector<float> cost(static_cast<size_t>(nodes), 1e30f);
  std::vector<int> from(static_cast<size_t>(nodes), -1);
  std::vector<char> closed(static_cast<size_t>(nodes), 0);
  struct Node {
    float f;
    int index;
    bool operator>(const Node &o) const { return f > o.f; }
  };
  std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
  cost[start] = 0;
  open.push({guess(start), start});
  std::vector<std::pair<int, float>> around;
  int expanded = 0;
  while (!open.empty() && expanded < maxNodes) {
    Node n = open.top();
    open.pop();
    if (closed[n.index])
      continue;
    closed[n.index] = 1;
    expanded++;
    if (n.index == goal)
      break;
    around.clear();
    next(n.index, around);
    for (auto [m, c] : around) {
      if (closed[m])
        continue;
      float total = cost[n.index] + c;
      if (total >= cost[m])
        continue;
      cost[m] = total;
      from[m] = n.index;
      open.push({total + guess(m), m});
    }
  }
  if (!closed[goal])
    return false;
  for (int at = goal; at >= 0; at = from[at]) {
    out.push_back(at);
    if (at == start)
      break;
  }
  std::reverse(out.begin(), out.end());
  return true;
}

// Through the tiles a segment crosses (in order), each crossing checked
bool clearLine(float x0, float y0, float x1, float y1, const Pass &pass, const Step &step) {
  int x = static_cast<int>(std::floor(x0)), y = static_cast<int>(std::floor(y0));
  int ex = static_cast<int>(std::floor(x1)), ey = static_cast<int>(std::floor(y1));
  float dx = x1 - x0, dy = y1 - y0;
  int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
  float tdx = dx != 0 ? std::fabs(1.0f / dx) : 1e30f;
  float tdy = dy != 0 ? std::fabs(1.0f / dy) : 1e30f;
  float tx = dx != 0 ? ((sx > 0 ? (x + 1 - x0) : (x0 - x)) * tdx) : 1e30f;
  float ty = dy != 0 ? ((sy > 0 ? (y + 1 - y0) : (y0 - y)) * tdy) : 1e30f;
  for (int guard = 0; guard < 1000 && (x != ex || y != ey); guard++) {
    if (std::fabs(tx - ty) < 1e-5f) {
      // Through a corner: both ways round it must be open
      if (!pass(x + sx, y) || !pass(x, y + sy) || !step(x, y, x + sx, y) ||
          !step(x, y, x, y + sy) || !step(x + sx, y, x + sx, y + sy) ||
          !step(x, y + sy, x + sx, y + sy))
        return false;
      x += sx;
      y += sy;
      tx += tdx;
      ty += tdy;
    } else if (tx < ty) {
      if (!step(x, y, x + sx, y))
        return false;
      x += sx;
      tx += tdx;
    } else {
      if (!step(x, y, x, y + sy))
        return false;
      y += sy;
      ty += tdy;
    }
    if ((x != ex || y != ey) && !pass(x, y))
      return false;
  }
  return true;
}

void smooth(std::vector<std::pair<float, float>> &points, const Pass &pass, const Step &step) {
  if (points.size() < 3)
    return;
  std::vector<std::pair<float, float>> out{points.front()};
  size_t at = 0;
  while (at + 1 < points.size()) {
    // The farthest point it can see from here
    size_t next = at + 1;
    for (size_t j = points.size() - 1; j > at + 1; j--)
      if (clearLine(points[at].first, points[at].second, points[j].first, points[j].second, pass,
                    step)) {
        next = j;
        break;
      }
    out.push_back(points[next]);
    at = next;
  }
  points = out;
}

} // namespace Pathfinder
