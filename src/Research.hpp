#ifndef RESEARCH_HPP
#define RESEARCH_HPP

#include <string>
#include <vector>

class ResourceManager;

// ============================================================================
// RESEARCH AND CONSERVATION
// ============================================================================
// research.cfg [branches] names the two branches (research/branres.cfg,
// research/brancon.cfg). A freeform zoo also has the expansions' (Dinosaur
// Digs researd.cfg, Marine Mania researe.cfg): their branches' categories
// follow the base game's in the branch of the same name (measured: 13
// research and 9 conservation categories). A branch file's [branch] has
// its name, its categories (category=research/catexN.cfg, in order), its
// funding levels (funding=none/min/normal/max, each a section: name =
// the "%s normal" string, cost = dollars, work = progress a day). A category file's
// [category] names its programs (program=research/progexN.cfg); a program's
// [research] has its name, icon, cost (work to finish it), order (programs
// of a lower order come first), target (the cNameID it unlocks or improves)
// and effect (0 unlock target, 1 an exhibit for target's house, 2 train
// target staff, 4/5 zoo-wide).
//
// Which program is researched: one of those that can be researched now
// (each checked category's lowest order left), picked at random - the
// original picked differently in two new games (Swinging log toy and
// Lowland Gorilla, then Nutritional animal food and Thouarsus Cycad).
// Funding starts at none.
// ============================================================================
struct ResearchProgram {
  std::string file;
  std::string name;
  std::string icon;
  int helpId = 0;
  int cost = 0;
  int order = 0;
  int target = 0;
  int effect = 0;
  int progress = 0; // work done
  bool done = false;
};

struct ResearchCategory {
  std::string file;
  std::string name;
  int expansion = 0; // 0 Zoo Tycoon, 1 Dinosaur Digs, 2 Marine Mania
  int helpId = 0;
  std::vector<ResearchProgram> programs;
};

struct ResearchFunding {
  std::string key; // none, min, normal, max
  std::string name; // "%s normal"
  int cost = 0;     // a day
  int work = 0;     // progress a day
};

struct ResearchBranch {
  std::string file;
  std::string name;
  std::string noProgramIcon;
  std::vector<ResearchCategory> categories;
  std::vector<ResearchFunding> funding;
  int fundingLevel = 0;
  std::vector<bool> enabled; // the categories' checkboxes (all on)
  int currentCategory = -1, currentProgram = -1;
};

class Research {
public:
  static Research &get();

  // Reads research.cfg and its files once (later calls do nothing)
  void load(ResourceManager *resource_manager);

  std::vector<ResearchBranch> &branches() { return this->list; }
  ResearchBranch *branch(int index) {
    return index >= 0 && index < (int)this->list.size() ? &this->list[index]
                                                        : nullptr;
  }
  // The program a branch is working on (nullptr: none left)
  ResearchProgram *current(ResearchBranch &branch);
  // The next program of a category (lowest order not done), if any can be
  // researched now
  ResearchProgram *nextIn(ResearchCategory &category);
  // Picks what a branch researches next
  void pick(ResearchBranch &branch);

private:
  std::vector<ResearchBranch> list;
  bool loaded = false;
  bool canResearch(const ResearchProgram &program) const;
};

#endif // RESEARCH_HPP
