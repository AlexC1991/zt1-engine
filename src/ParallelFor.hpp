#ifndef PARALLEL_FOR_HPP
#define PARALLEL_FOR_HPP

#include <algorithm>
#include <cstddef>
#include <future>
#include <thread>
#include <vector>

// ours: a loop over n items split over the processor's cores: fn(begin, end)
// for each part, the parts writing apart from each other. The same work as
// one loop, done side by side; under minItems it is one loop on this thread.
// (The day and night lighting's rebuilds - the sun maps' samples, the lamps'
// rays, the shapes' projection - ran on one core while the game waited.)
template <typename F> void parallelFor(size_t n, F &&fn, size_t minItems = 4096) {
  const size_t threads = std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 8);
  if (n < minItems || n < 2 || threads < 2) {
    fn(size_t(0), n);
    return;
  }
  const size_t chunk = (n + threads - 1) / threads;
  std::vector<std::future<void>> parts;
  for (size_t b = chunk; b < n; b += chunk)
    parts.push_back(std::async(std::launch::async, [&fn, b, chunk, n] { fn(b, std::min(n, b + chunk)); }));
  fn(size_t(0), std::min(n, chunk));
  for (auto &part : parts)
    part.get();
}

#endif // PARALLEL_FOR_HPP
