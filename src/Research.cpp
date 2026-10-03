#include "Research.hpp"

#include <mutex>
#include <random>

#include <SDL2/SDL.h>

#include "IniReader.hpp"
#include "ItemCatalog.hpp"
#include "ResourceManager.hpp"

Research &Research::get() {
  static Research research;
  return research;
}

static std::mt19937 &rng() {
  static std::mt19937 generator{std::random_device{}()};
  return generator;
}

void Research::load(ResourceManager *rm) {
  static std::mutex loading; // see ItemCatalog::load
  std::lock_guard<std::mutex> lock(loading);
  if (this->loaded)
    return;
  this->loaded = true;
  ItemCatalog::get().load(rm);

  auto text = [rm](IniReader *ini, const std::string &section,
                   const std::string &key) {
    int id = ini->getInt(section, key, 0);
    return id ? rm->getString(id) : std::string();
  };

  // Each branch file, and which pack's research config listed it
  std::vector<std::pair<std::string, int>> branchFiles;
  const char *configs[] = {"research.cfg", "researd.cfg", "researe.cfg"};
  for (int pack = 0; pack < 3; pack++) {
    IniReader *root = rm->getIniReader(configs[pack]);
    if (!root)
      continue;
    for (const std::string &f : root->getList("branches", "branch"))
      branchFiles.push_back({f, pack});
    delete root;
  }
  for (const auto &[branchFile, pack] : branchFiles) {
    IniReader *b = rm->getIniReader(branchFile);
    if (!b)
      continue;
    ResearchBranch branch;
    branch.file = branchFile;
    branch.name = text(b, "branch", "name");
    branch.noProgramIcon = b->get("branch", "noprogicon");
    for (const std::string &key : b->getList("branch", "funding")) {
      ResearchFunding f;
      f.key = key;
      f.name = text(b, key, "name");
      f.cost = b->getInt(key, "cost", 0);
      f.work = b->getInt(key, "work", 0);
      branch.funding.push_back(f);
    }
    for (const std::string &categoryFile : b->getList("branch", "category")) {
      IniReader *c = rm->getIniReader(categoryFile);
      if (!c)
        continue;
      ResearchCategory category;
      category.file = categoryFile;
      category.expansion = pack;
      category.name = text(c, "category", "name");
      category.helpId = c->getInt("category", "helpid", 0);
      for (const std::string &programFile : c->getList("category", "program")) {
        IniReader *p = rm->getIniReader(programFile);
        if (!p)
          continue;
        ResearchProgram program;
        program.file = programFile;
        program.name = text(p, "research", "name");
        program.icon = p->get("research", "icon");
        program.helpId = p->getInt("research", "helpid", 0);
        program.cost = p->getInt("research", "cost", 0);
        program.order = p->getInt("research", "order", 0);
        program.target = p->getInt("research", "target", 0);
        program.effect = p->getInt("research", "effect", 0);
        category.programs.push_back(program);
        delete p;
      }
      branch.categories.push_back(category);
      delete c;
    }
    delete b;
    // An expansion's branch adds its categories to the base game's
    ResearchBranch *same = nullptr;
    for (ResearchBranch &existing : this->list)
      if (existing.name == branch.name)
        same = &existing;
    if (same)
      same->categories.insert(same->categories.end(), branch.categories.begin(),
                              branch.categories.end());
    else
      this->list.push_back(branch);
  }
  for (ResearchBranch &branch : this->list) {
    branch.enabled.assign(branch.categories.size(), true);
    this->pick(branch);
  }
  for (ResearchBranch &branch : this->list) {
    ResearchProgram *p = this->current(branch);
    SDL_Log("[Research] %s: %zu categories, researching %s", branch.name.c_str(),
            branch.categories.size(), p ? p->name.c_str() : "nothing");
  }
}

// Whether a program can be researched now: a new animal or object only
// while the zoo can't buy it yet; a house's exhibit, or training for a kind
// of staff, once the zoo has that house or staff
bool Research::canResearch(const ResearchProgram &program) const {
  const CatalogItem *target =
      program.target ? ItemCatalog::get().findName(program.target) : nullptr;
  bool available = target && target->unlockMonth == 0;
  switch (program.effect) {
  case 0:
    return !available;
  case 1:
  case 2:
    return program.target == 0 || available;
  default:
    return true;
  }
}

ResearchProgram *Research::nextIn(ResearchCategory &category) {
  // The lowest order with programs left is the one being worked through
  int lowest = -1;
  for (const ResearchProgram &p : category.programs)
    if (!p.done && (lowest < 0 || p.order < lowest) && this->canResearch(p))
      lowest = p.order;
  for (ResearchProgram &p : category.programs)
    if (!p.done && p.order == lowest && this->canResearch(p))
      return &p;
  return nullptr;
}

void Research::pick(ResearchBranch &branch) {
  branch.currentCategory = branch.currentProgram = -1;
  // Any program of a checked category that can be researched now (each
  // category's lowest order), at random
  std::vector<std::pair<int, int>> candidates;
  for (int c = 0; c < (int)branch.categories.size(); c++) {
    if (c < (int)branch.enabled.size() && !branch.enabled[c])
      continue;
    ResearchCategory &category = branch.categories[c];
    int lowest = -1;
    for (const ResearchProgram &p : category.programs)
      if (!p.done && this->canResearch(p) && (lowest < 0 || p.order < lowest))
        lowest = p.order;
    for (int i = 0; i < (int)category.programs.size(); i++) {
      const ResearchProgram &p = category.programs[i];
      if (!p.done && p.order == lowest && this->canResearch(p))
        candidates.push_back({c, i});
    }
  }
  if (candidates.empty())
    return;
  std::uniform_int_distribution<size_t> pickOne(0, candidates.size() - 1);
  auto chosen = candidates[pickOne(rng())];
  branch.currentCategory = chosen.first;
  branch.currentProgram = chosen.second;
}

ResearchProgram *Research::current(ResearchBranch &branch) {
  if (branch.currentCategory < 0 ||
      branch.currentCategory >= (int)branch.categories.size())
    return nullptr;
  ResearchCategory &c = branch.categories[branch.currentCategory];
  if (branch.currentProgram < 0 || branch.currentProgram >= (int)c.programs.size())
    return nullptr;
  return &c.programs[branch.currentProgram];
}
