#include "LabTrace.hpp"

namespace LabTrace {

bool on = false;
int muted = 0;
std::function<void(const Step &)> sink;

namespace {
std::vector<Frame> frames;
std::vector<Who> whos;
} // namespace

void step(const char *file, int line, const char *fn, const char *zoo, const char *what,
          std::initializer_list<Val> vals) {
  if (on && !muted && sink)
    sink(Step{file, line, fn, zoo, what, vals, false});
}

const std::vector<Frame> &stack() { return frames; }

Who who() { return whos.empty() ? Who{} : whos.back(); }

Fn::Fn(const char *file, int line, const char *fn, const char *zoo, const char *what) {
  if (!on)
    return;
  frames.push_back({file, line, fn, zoo, what});
  pushed = true;
  if (!muted && sink)
    sink(Step{file, line, fn, zoo, what, {}, true});
}

Fn::~Fn() {
  if (pushed && !frames.empty())
    frames.pop_back();
}

Scope::Scope(const char *kind, int id) {
  if (!on)
    return;
  whos.push_back({kind, id});
  pushed = true;
}

Scope::~Scope() {
  if (pushed && !whos.empty())
    whos.pop_back();
}

} // namespace LabTrace
