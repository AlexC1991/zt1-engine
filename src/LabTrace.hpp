#ifndef LAB_TRACE_HPP
#define LAB_TRACE_HPP

// The engine's code reporting its steps to the ZT1 Lab: where it is (file,
// line, function), which zoo.exe function that code is ours of, what it
// read and what it changed. The Lab's Scene view shows them as they happen
// and can stop the engine at one (a breakpoint) and step on from there,
// the view drawn as the world is at that line.
//
//   ZT_WHO("animal", m.id);                        // whose code this is
//   ZT_FN("0x438a79", "Hunger check");             // a function: a frame
//   float was = m.hunger;
//   m.hunger = std::min(100.0f, m.hunger + t.hungerIncrement);
//   ZT_STEP("0x438a79", "Hungrier", {"hunger", was, m.hunger},
//           {"cHungerIncrement", t.hungerIncrement});
//
// A value is {name, value}, {name, before, after} or {name, "text"}. With
// nobody listening (the game itself) each is a test of one bool.
#include <cmath>
#include <functional>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

namespace LabTrace {

struct Val {
  const char *name;
  double to = 0, from = NAN;
  std::string text;
  bool isText = false;
  // (any number: ints, floats, bools - taken as they are, so a brace list
  // of them is no narrowing)
  template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
  Val(const char *n, T v) : name(n), to(static_cast<double>(v)) {}
  template <typename A, typename B,
            typename = std::enable_if_t<std::is_arithmetic_v<A> && std::is_arithmetic_v<B>>>
  Val(const char *n, A before, B after) : name(n), to(static_cast<double>(after)), from(static_cast<double>(before)) {}
  Val(const char *n, const std::string &s) : name(n), text(s), isText(true) {}
  Val(const char *n, const char *s) : name(n), text(s ? s : ""), isText(true) {}
};

struct Frame {
  const char *file;
  int line;
  const char *fn, *zoo, *what;
};

struct Who {
  const char *kind = nullptr; // animal guest staff exhibit tool ... (nullptr: the world)
  int id = -1;
};

struct Step {
  const char *file;
  int line;
  const char *fn, *zoo, *what;
  std::initializer_list<Val> vals;
  bool enter; // a function starting (ZT_FN)
};

// Anyone listening (the Lab sets it); muted while the Lab itself reads the
// world (its inspector, its outline): those aren't the game's steps
extern bool on;
extern int muted;
extern std::function<void(const Step &)> sink;

void step(const char *file, int line, const char *fn, const char *zoo, const char *what,
          std::initializer_list<Val> vals);
const std::vector<Frame> &stack();
Who who();

struct Fn {
  bool pushed = false;
  Fn(const char *file, int line, const char *fn, const char *zoo, const char *what);
  ~Fn();
};
struct Scope {
  bool pushed = false;
  Scope(const char *kind, int id);
  ~Scope();
};
struct Mute {
  Mute() { muted++; }
  ~Mute() { muted--; }
};

} // namespace LabTrace

#define ZT_STEP(zoo, what, ...)                                                                    \
  do {                                                                                             \
    if (LabTrace::on)                                                                              \
      LabTrace::step(__FILE__, __LINE__, __FUNCTION__, zoo, what, {__VA_ARGS__});                  \
  } while (0)
#define ZT_FN(zoo, what) LabTrace::Fn zt_fn_(__FILE__, __LINE__, __FUNCTION__, zoo, what)
#define ZT_WHO(kind, id) LabTrace::Scope zt_who_(kind, id)

// Readers of the game's files, each registered by the file it reads, so the
// ZT1 Lab can run one on its own (Engine code > Build & run): what it read
// comes out as steps. A reader is engine code like any other: the engine
// calls the same function where it needs the file.
//
//   std::string readThing(ResourceManager *rm) { ...; return "a summary"; }
//   ZT_GAMEFILE_READER("ui/thing.cfg", Thing, readThing);
class ResourceManager;
namespace GameFileReaders {
struct Reader {
  std::string file; // (lower case, as the game's archives name it)
  std::string name;
  std::string (*run)(ResourceManager *);
};
inline std::vector<Reader> &all() {
  static std::vector<Reader> readers;
  return readers;
}
struct Add {
  Add(const char *file, const char *name, std::string (*fn)(ResourceManager *)) { all().push_back({file, name, fn}); }
};
} // namespace GameFileReaders
#define ZT_GAMEFILE_READER(file, name, fn) static GameFileReaders::Add zt_reader_##name(file, #name, fn)

#endif
