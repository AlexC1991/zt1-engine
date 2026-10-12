#ifndef PATHS_HPP
#define PATHS_HPP

#include <string>

// ours: where the engine finds the game and keeps its own things. It runs one
// of two ways:
//
// - Portable: Zoo Tycoon's files are next to the exe (the source checkout's
//   build/Release, a zip set up by start.bat, the ZT1 Lab). Everything is
//   where it always was: the game's files, zoo.ini and mods beside the exe.
// - Installed: the engine on its own (the installer's folder). It reads the
//   player's own Zoo Tycoon where it's installed - found in the registry or
//   the usual folders, else asked for once - and never writes there. Its
//   settings (zoo.ini), mods and scenario progress are kept in
//   %LocalAppData%\zt1-engine, so an update never touches them.
//
// Saved zoos and snapshots go to Documents\zt1-engine either way.
namespace Paths {

// At start, before anything is read: works out which way it's running,
// finds the game (asking the player when it has to), makes the data folder,
// and makes the game's folder the current one (the game's own paths, from
// zoo.ini's resource path on, are relative to it). False: there's no game
// to run - the player was told why.
bool setup();

bool portable();
// The exe's folder: the engine's own files (fonts)
std::string appDir();
// The player's Zoo Tycoon
std::string gameDir();
// The engine's settings, mods and scenario progress
std::string dataDir();
// zoo.ini: the game's paths and the player's settings
std::string configFile();
// Which scenarios are done (ScenarioDatabase's state)
std::string scenarioProgressFile();

} // namespace Paths

#endif // PATHS_HPP
