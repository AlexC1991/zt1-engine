#ifndef PATHFINDER_HPP
#define PATHFINDER_HPP

#include <functional>
#include <utility>
#include <vector>

// A* over the tile grid, for anything that walks (staff now; guests and
// animals later). Eight directions: a diagonal step only where both of the
// straight steps round its corner are allowed (no cutting past a fence end
// or a cliff). Then the route is pulled straight wherever there's a clear
// line, so walkers cross open ground on the diagonal rather than in steps.
namespace Pathfinder {
using Pass = std::function<bool(int x, int y)>;
using Step = std::function<bool(int x, int y, int nx, int ny)>;
// What entering a tile costs (1 = a path; open ground more), or none: all 1
using Cost = std::function<float(int x, int y)>;

// Tiles from start to goal (both included); the goal itself needn't pass
// (a keeper's gate tile, a fence it stands beside). False: no way there.
bool find(int width, int height, int sx, int sy, int gx, int gy, const Pass &pass,
          const Step &step, std::vector<std::pair<int, int>> &out, int maxNodes = 40000,
          const Cost &cost = nullptr);

// A* over any graph of nodes (walkways: a ground and a deck layer):
// 'next' lists a node's neighbours with their step cost, 'guess' the
// remaining cost (never more than it really is)
using Next = std::function<void(int node, std::vector<std::pair<int, float>> &out)>;
using Guess = std::function<float(int node)>;
bool findGraph(int nodes, int start, int goal, const Next &next, const Guess &guess,
               std::vector<int> &out, int maxNodes = 80000);

// Whether a straight walk from one point to another (tiles) stays on
// passable tiles and crosses only allowed steps
bool clearLine(float x0, float y0, float x1, float y1, const Pass &pass, const Step &step);

// Drops waypoints that can be walked past in a straight line
void smooth(std::vector<std::pair<float, float>> &points, const Pass &pass, const Step &step);
} // namespace Pathfinder

#endif // PATHFINDER_HPP
