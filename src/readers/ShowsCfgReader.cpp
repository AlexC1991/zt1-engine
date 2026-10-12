// shows.cfg: the game's, read by our engine. Every value it holds comes out
// as a step (Engine code > Build & run, then Run). When the zoo.exe function
// that reads it is found, put its address in ZT_FN below, and use readShowsCfg
// where the game needs this file.
#include "../IniReader.hpp"
#include "../LabTrace.hpp"
#include "../ResourceManager.hpp"

#include <string>

namespace {
std::string readShowsCfg(ResourceManager *rm) {
  ZT_FN("", "Reads shows.cfg");
  IniReader *ini = rm->getIniReader("shows.cfg");
  if (!ini) {
    ZT_STEP("", "It isn't there", {"file", "shows.cfg"});
    return "not there";
  }
  int sections = 0, values = 0;
  for (const std::string &section : ini->getSections()) {
    sections++;
    for (const auto &[key, value] : ini->getSection(section)) {
      values++;
      ZT_STEP("", "A value", {"section", section}, {"key", key}, {"value", value});
    }
  }
  delete ini;
  return std::to_string(sections) + " sections, " + std::to_string(values) + " values";
}
} // namespace

ZT_GAMEFILE_READER("shows.cfg", ShowsCfg, readShowsCfg);
