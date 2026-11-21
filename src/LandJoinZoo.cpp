#include "World.hpp"
#include "Shows.hpp"
#include "SaveGame.hpp"
#include "ZooSim.hpp"
#include "ui/UiGameScreen.hpp"
#include <cmath>
#include <cstdlib>
#include <sstream>

// ours: ZT_LANDJOIN_ZOO=<file.zoo> (ZT_FLAT_LAB=1) - a sunken pool between two
// land exhibits, each sharing one of the pool's walls, with a walrus (an
// amphibious animal) in the pool and a different look to each land (the
// second has trees and rocks, the first nothing), saved as a shared .zoo the
// original loads too. A measuring aid for #316: which of the two lands the
// original gives the walrus as its habitat (unitHabitatNow 0x4161da ->
// habitatPickForAnimal 0x4161f5 -> habitatBestRated 0x498a05; the walrus's
// +0x3b4 holds the picked habitat's gate tile)
std::string buildLandJoinZoo(World &world, ZooSim &sim, UiGameScreen &hud, const std::string &mapPath,
                             const std::string &file) {
  auto &f = world.getFences();
  auto &map = world.getMap();
  auto &a = world.getAnimals();
  auto &st = world.getStaff();
  auto &shows = world.getShows();
  shows.clear();
  a.clear();
  st.clear();
  // ZT_LANDJOIN_LIST=1: the amphibious kinds that need a shelter or toys
  if (std::getenv("ZT_LANDJOIN_LIST"))
    for (size_t k = 0; k < a.types().size(); ++k) {
      const Animals::Type &t = a.types()[k];
      if (t.underwater && !t.onlyUnderwater)
        SDL_Log("[LandJoinZoo] amphibious %s shelter %d toys %d", t.key.c_str(), t.needShelter ? 1 : 0, t.needToys ? 1 : 0);
    }
  int drag = 10;
  auto rectangle = [&](int x, int y, int w, int h, int type, const std::string &name) {
    int base = drag;
    drag += 4;
    for (int i = x; i < x + w; i++) {
      f.place({true, i, y}, type, base, map);
      f.place({true, i, y + h}, type, base + 1, map);
    }
    for (int j = y; j < y + h; j++) {
      f.place({false, x, j}, type, base + 2, map);
      f.place({false, x + w, j}, type, base + 3, map);
    }
    for (int id : f.updateExhibits(map, 1, 0, 1))
      f.finishExhibit(id, name, map);
    return f.exhibitAt(x, y);
  };
  // (the pool first: its walls on the lands' shared sides stay, the lands'
  // chain link goes round the rest)
  // ZT_LANDJOIN_LAYOUT: 1 (default) the pool between two lands, each with one
  // of its walls; 2 the pool inside one land; 3 the pool inside a land with a
  // second land nested inside it, sharing the pool's right wall
  const char *layoutText = std::getenv("ZT_LANDJOIN_LAYOUT");
  const int layout = layoutText ? std::atoi(layoutText) : 1;
  int pool = -1, landA = -1, landB = -1;
  if (layout == 2) {
    landA = rectangle(40, 25, 20, 18, f.typeIndex("chainlnk"), "Land A");
    pool = rectangle(48, 30, 8, 8, f.typeIndex("atltank"), "Joined pool");
    landB = landA;
  } else if (layout == 4) {
    // the two rules disagree: land A shares more of the pool's wall (12
    // edges against 6) but is the worse land (trees and rocks the walrus
    // dislikes), land B the better and smaller one
    pool = rectangle(50, 28, 6, 12, f.typeIndex("atltank"), "Joined pool");
    landA = rectangle(40, 28, 10, 12, f.typeIndex("chainlnk"), "Land A");
    landB = rectangle(56, 30, 10, 6, f.typeIndex("chainlnk"), "Land B");
  } else if (layout == 3) {
    landA = rectangle(38, 25, 30, 18, f.typeIndex("chainlnk"), "Land A");
    pool = rectangle(46, 30, 6, 8, f.typeIndex("atltank"), "Joined pool");
    landB = rectangle(52, 30, 6, 8, f.typeIndex("chainlnk"), "Land B");
  } else {
    pool = rectangle(50, 28, 6, 12, f.typeIndex("atltank"), "Joined pool");
    landA = rectangle(40, 28, 10, 12, f.typeIndex("chainlnk"), "Land A");
    landB = rectangle(56, 28, 10, 12, f.typeIndex("chainlnk"), "Land B");
  }
  if (landA < 0 || landB < 0 || pool < 0)
    return "the exhibits didn't form";
  if (Fences::Exhibit *p = f.exhibit(pool))
    p->water = 1;
  // ZT_LANDJOIN_LOWER=<n>: the pool's walls taken down n steps (a pool level
  // with the ground round it)
  if (const char *lower = std::getenv("ZT_LANDJOIN_LOWER"))
    for (int k = std::atoi(lower); k > 0 && f.canAdjustWall(pool, -1); --k)
      f.adjustWall(pool, -1);
  world.reindex();
  // (the second land looks better: trees and rocks, the first bare)
  auto &objects = world.objectsMutable();
  int placed = 0;
  const float treeX = layout == 3 ? 53.5f : layout == 4 ? 42.5f : 58.5f;
  if (layout != 2)
    for (const char *tree : {"scenery/foliage/asitree.ai", "scenery/other/lrock1.ai"})
      for (int k = 0; k < 6; ++k) {
        objects.add(tree, treeX + (k % 3) * (layout == 3 ? 1.0f : 2.0f), 30.5f + (k / 3) * 3.0f + (tree[9] == 'o' ? 0.5f : 0.0f), 0);
        ++placed;
      }
  // ZT_LANDJOIN_POOLOBJ=<file.ai>: three of that object in the pool (layout 2:
  // the pool at 48..56 x 30..38), to see what the land's rating makes of them
  if (const char *poolObj = std::getenv("ZT_LANDJOIN_POOLOBJ"))
    for (int k = 0; k < 3; ++k) {
      objects.add(poolObj, 50.5f + k * 2.0f, 32.5f + k * 1.5f, 0);
      ++placed;
    }
  // ZT_LANDJOIN_LANDOBJ=<file.ai>: one of that building on the land (layout 2,
  // at 43.5, 34.5), for the tank animal's shelter and toys (0x507150)
  if (const char *landObj = std::getenv("ZT_LANDJOIN_LANDOBJ")) {
    objects.add(landObj, 43.5f, 34.5f, 0);
    ++placed;
  }
  world.reindex();
  for (int k = 0; k < 60; ++k)
    world.update(SDL_GetKeyboardState(nullptr), 0.05f);
  const int swimmer = a.adopt(a.typeOfFile(std::getenv("ZT_LANDJOIN_SPECIES") ? std::getenv("ZT_LANDJOIN_SPECIES") : "animals/walrus.ai"), false, layout == 1 || layout == 4 ? 52.5f : layout == 2 ? 51.5f : 48.5f, 33.5f, map, f, true);
  if (swimmer < 0)
    return "no walrus";
  // ZT_LANDJOIN_ZEBRA=1: a zebra on the land too (layout 2), a land animal
  // that isn't amphibious
  int zebra = -1;
  if (std::getenv("ZT_LANDJOIN_ZEBRA"))
    zebra = a.adopt(a.typeOfFile("animals/zebra.ai"), false, 43.5f, 34.5f, map, f, true);
  for (int k = 0; k < 40; ++k)
    world.update(SDL_GetKeyboardState(nullptr), 0.05f);
  world.reindex();
  // ZT_LANDJOIN_CHECK=1 (with layout 4): the walrus must count as in the
  // best rated land (B) though A shares more of the pool's wall - zoo.exe
  // habitatBestRated 0x498a05, measured in the original (#316)
  if (const char *pickChk = std::getenv("ZT_LANDJOIN_CHECK"); pickChk && std::atoi(pickChk) == 1) {
    const Animals::Member *w = a.member(swimmer);
    const float scoreA = a.landScore(landA, w ? w->type : -1, map, f), scoreB = a.landScore(landB, w ? w->type : -1, map, f);
    const bool pass = layout == 4 && w && f.habitatRoot(pool, map) == landA && scoreB > scoreA && w->exhibit == landB;
    SDL_Log("[LandJoinZoo] %s the walrus in the pool is filed under %d (land A %d scores %.1f, land B %d %.1f, most shared wall: %d)",
            pass ? "PASS" : "FAIL", w ? w->exhibit : -1, landA, scoreA, landB, scoreB, f.habitatRoot(pool, map));
    return pass ? "ok best rated pick" : "the best rated pick failed";
  }
  // ZT_LANDJOIN_RATE=1: ours' rating records for the walrus, to set against
  // the original's (habitat +0x148 map: terrain +0x20, objects +0x24, trees
  // +0x2c, rocks +0x34, elevation +0x38, shelters +0x44, toys +0x48)
  if (std::getenv("ZT_LANDJOIN_RATE")) {
    const Animals::Member *w = a.member(zebra >= 0 ? zebra : swimmer);
    for (int id : {pool, landA}) {
      a.evalExtraKind = w ? w->type : -1;
      a.evaluate(id, map, f);
      a.evalExtraKind = -1;
      const auto it = a.evaluations[id].kinds.find(w ? w->type : -1);
      if (it != a.evaluations[id].kinds.end()) {
        const Animals::Rating &r = it->second;
        SDL_Log("[LandJoinZoo] rating #%d (%s): terrain %.1f objects %.1f trees %.1f rocks %.1f elevation %.1f shelters %.1f toys %.1f", id,
                f.exhibit(id)->tank ? "tank" : "land", r.terrain, r.objects, r.trees, r.rocks, r.elevation, r.shelters, r.toys);
      }
    }
  }
  // ZT_LANDJOIN_CHECK=2 (layout 2, ZT_LANDJOIN_ZEBRA=1, ZT_LANDJOIN_POOLOBJ=kelp):
  // the land's record for the zebra is its own tiles alone (the original's:
  // terrain 0, objects 0, trees 84, rocks 88) and the walrus's takes the
  // pool's kelp in (trees 12.9) - zoo.exe habitatJoinedObjectsForAmphibians
  // 0x507645, measured (#316)
  // ZT_LANDJOIN_CHECK=3 (layout 2, ZT_LANDJOIN_SPECIES=animals/polarbr.ai,
  // ZT_LANDJOIN_LANDOBJ=scenery/building/icecave.ai): the land's objects score
  // for the bear divides by the land's own tiles (the original's -69.3)
  if (const char *chk3 = std::getenv("ZT_LANDJOIN_CHECK"); chk3 && std::atoi(chk3) == 3) {
    const Animals::Member *w = a.member(swimmer);
    a.evaluate(landA, map, f);
    const auto &kinds = a.evaluations[landA].kinds;
    const auto wi = w ? kinds.find(w->type) : kinds.end();
    const bool have = wi != kinds.end();
    const bool pass = have && std::fabs(wi->second.objects + 69.3f) < 0.3f;
    SDL_Log("[LandJoinZoo] %s the land's objects score for the bear %.1f (the original's -69.3)", pass ? "PASS" : "FAIL",
            have ? wi->second.objects : 0.f);
    return pass ? "ok joined objects" : "the joined objects failed";
  }
  if (const char *chk = std::getenv("ZT_LANDJOIN_CHECK"); chk && std::atoi(chk) == 2) {
    const Animals::Member *z = a.member(zebra), *w = a.member(swimmer);
    a.evaluate(landA, map, f);
    const auto &kinds = a.evaluations[landA].kinds;
    const auto zi = z ? kinds.find(z->type) : kinds.end();
    const auto wi = w ? kinds.find(w->type) : kinds.end();
    const bool have = zi != kinds.end() && wi != kinds.end();
    const bool pass = have && std::fabs(zi->second.terrain) < 0.05f && std::fabs(zi->second.objects) < 0.05f &&
                      std::fabs(zi->second.trees - 84.0f) < 0.5f && std::fabs(wi->second.trees - 12.9f) < 0.1f;
    SDL_Log("[LandJoinZoo] %s the land's records: zebra terrain %.1f objects %.1f trees %.1f, walrus trees %.1f", pass ? "PASS" : "FAIL",
            have ? zi->second.terrain : -1.f, have ? zi->second.objects : -1.f, have ? zi->second.trees : -1.f, have ? wi->second.trees : -1.f);
    return pass ? "ok joined rating" : "the joined rating failed";
  }
  const bool joined = f.habitatGroup(pool, map).size() > 1;
  const int root = f.habitatRoot(pool, map);
  if (!SaveGame::save(file, world, sim, hud, mapPath))
    return "save failed: " + SaveGame::lastError();
  for (const Fences::Exhibit &e : f.exhibits())
    SDL_Log("[LandJoinZoo] exhibit #%d tank %d show %d named %d pocket %d filling %d water %.2f tiles %zu animals %d purity %.0f", e.id, e.tank,
            e.show, e.named, e.pocket, e.filling, e.water, e.tiles.size(), e.animals, e.purity);
  std::ostringstream r;
  r << "ok land A #" << landA << " land B #" << landB << " pool #" << pool << " joined " << joined << " ours root #"
    << root << " objects " << placed << " walrus " << swimmer;
  return r.str();
}
