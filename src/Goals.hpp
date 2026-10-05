#ifndef GOALS_HPP
#define GOALS_HPP

#include <functional>
#include <string>
#include <vector>

class ResourceManager;

// The scenario goal engine (zoo.exe 0x58de8a loads, 0x4240f5 checks,
// 0x4fc783 fires): a map's .scn [goals] goal= and each extragoals= file's
// (freeform: donation.scn, awards.scn, unlock.scn). Every 1.5 s of game
// time each goal measures its rule (rulea, ruleb, arga, argb) against
// value by type; when its state changes to met it fires its trigger
// (trulea, truleb, targa, targb): a popup, a UI element disabled or enabled,
// cash, an award. Sticky goals fire once, ever.
class Goals {
public:
  struct Goal {
    std::string name, file;
    int rulea = 0, ruleb = 0, type = 0, value = 0, arga = 0, argb = 0;
    bool sticky = false, hidden = false, optional = false;
    int text = 0;
    int trulea = 0, truleb = 0, targa = 0, targb = 0;
    int state = 0;
  };
  // What the engine asks of the game
  struct Hooks {
    // A rule's measure (zoo.exe 0x41d665)
    std::function<int(const Goal &)> measure;
    // A popup: its picture and text; pause=1: the game waits on it
    std::function<void(const std::string &image, const std::string &text, bool pause)> popup;
    // A UI element disabled (true) or enabled; true when that changed it
    std::function<bool(int element, bool disable)> disable;
    std::function<void(int element, bool hide)> hide;
    // Cash: 0 booked and added, 1 added, 2 set to
    std::function<void(int amount, int mode)> cash;
    std::function<void(int stringId)> message;
    std::function<void(int award)> award;
  };

  void setHooks(Hooks hooks) { this->hooks = std::move(hooks); }
  // A map's goals (and its [start] triggers= fired once)
  void load(ResourceManager *rm, const std::string &scnPath);
  void clear();
  void update(float seconds);
  // Every goal checked now (tests)
  void evaluateAll();
  const std::vector<Goal> &all() const { return this->goals; }
  std::vector<Goal> &mutableAll() { return this->goals; }
  // Awards received, in order
  std::vector<int> awards;
  // A saved game's states put back: what they did done again, silently
  // (no popups, no cash: zoo.exe 0x595083)
  void restore(const std::vector<int> &states);

private:
  ResourceManager *rm = nullptr;
  std::vector<Goal> goals;
  Hooks hooks;
  float clock = 0;
  void readGoals(const std::string &file, bool fromList);
  void evaluate(Goal &g);
  void fire(const Goal &g, bool quiet);
  void showPopup(const std::string &file, int section);
};

#endif // GOALS_HPP
