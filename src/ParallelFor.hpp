#ifndef PARALLEL_FOR_HPP
#define PARALLEL_FOR_HPP

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// ours: a few worker threads kept for the whole run, waiting for parts of a
// parallelFor to help with (std::async per part asked the system for a
// thread each time: one it started late, the cores busy elsewhere, held the
// frame up until it had run - 50-200 ms frames now and then)
namespace parallel_detail {
class Pool {
public:
  static Pool &get() {
    static Pool pool(std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 8) - 1);
    return pool;
  }
  size_t size() const { return this->workers.size(); }
  void post(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lock(this->mutex);
      this->tasks.push_back(std::move(task));
    }
    this->wake.notify_one();
  }
  ~Pool() {
    {
      std::lock_guard<std::mutex> lock(this->mutex);
      this->stopping = true;
    }
    this->wake.notify_all();
    for (auto &w : this->workers)
      w.join();
  }

private:
  explicit Pool(size_t count) {
    for (size_t i = 0; i < count; ++i)
      this->workers.emplace_back([this] {
        for (;;) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(this->mutex);
            this->wake.wait(lock, [this] { return this->stopping || !this->tasks.empty(); });
            if (this->stopping && this->tasks.empty())
              return;
            task = std::move(this->tasks.front());
            this->tasks.pop_front();
          }
          task();
        }
      });
  }
  std::vector<std::thread> workers;
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::function<void()>> tasks;
  bool stopping = false;
};
} // namespace parallel_detail

// ours: a loop over n items split over the processor's cores: fn(begin, end)
// for each part, the parts writing apart from each other. The same work as
// one loop, done side by side; under minItems it is one loop on this thread.
// (The day and night lighting's rebuilds - the sun maps' samples, the lamps'
// rays, the shapes' projection - ran on one core while the game waited.)
// The parts - a few per core - are taken one at a time by whoever is free,
// this thread too, so a helper that starts late finds them done and leaves:
// this thread waits only for parts already being worked on. Each part is
// done once, by one thread, as before.
template <typename F> void parallelFor(size_t n, F &&fn, size_t minItems = 4096) {
  auto &pool = parallel_detail::Pool::get();
  const size_t threads = pool.size() + 1;
  if (n < minItems || n < 2 || threads < 2) {
    fn(size_t(0), n);
    return;
  }
  const size_t parts = std::min(n, threads * 4);
  const size_t chunk = (n + parts - 1) / parts;
  const size_t count = (n + chunk - 1) / chunk;
  struct Shared {
    std::atomic<size_t> next{0};
    std::atomic<size_t> finished{0};
    std::mutex mutex;
    std::condition_variable done;
  };
  auto shared = std::make_shared<Shared>();
  // (fn is only called for a part taken before every part was finished, and
  // this thread waits for those: fn outlives every call)
  auto *work = &fn;
  auto run = [shared, work, chunk, count, n] {
    for (;;) {
      const size_t part = shared->next.fetch_add(1);
      if (part >= count)
        return;
      (*work)(part * chunk, std::min(n, (part + 1) * chunk));
      if (shared->finished.fetch_add(1) + 1 == count) {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->done.notify_all();
      }
    }
  };
  const size_t helpers = std::min(pool.size(), count - 1);
  for (size_t i = 0; i < helpers; ++i)
    pool.post(run);
  run();
  std::unique_lock<std::mutex> lock(shared->mutex);
  shared->done.wait(lock, [&] { return shared->finished.load() == count; });
}

#endif // PARALLEL_FOR_HPP
