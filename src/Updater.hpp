#ifndef UPDATER_HPP
#define UPDATER_HPP

#include <string>

// ours: updates for installed, official builds, the way VoxAI Coder does
// them. The feed is CodeRook's zt1-engine release (zt1-update.yml), else
// GitHub's latest (latest.yml). Either carries an ECDSA P-256 signature over
// the app, version, installer name, size and SHA-512, verified against the key
// pinned here before anything is downloaded. Looked at 15 s after start, every
// 30 min (5 after a look that failed) and on Check now (Game Options › Mods &
// Updates). A newer release downloads in the background into the data
// folder's updates\ and its size and SHA-512 are checked; only then is the
// player asked, at the main menu: Update now (it closes, installs, starts
// again), Skip this version, or Later (installed quietly when the game
// closes). The installer is checked again just before it runs.
//
// Only official builds (made by the release tooling, ZT1_OFFICIAL_BUILD)
// ever update: a copy someone built themselves keeps their own code. The
// "updatechecks" switch (Features.hpp) turns the timed looks off (Check now
// still looks). Never in a portable copy (the checkout, the Lab's Play, the
// Game Build zip): nothing there was installed. ZT1_UPDATE_FEED (a folder URL
// ending in /) takes the feeds' place, for trying an update out.
namespace Updater {

struct Release {
  std::string version; // "2026.10.19"
  std::string file;    // the installer's name in the feed
  std::string url;     // where to download it
  std::string sha512;  // base64, as the feed gives it
  std::string signature; // base64 ECDSA P-256 signature (IEEE P1363)
  std::string size;      // signed installer size in bytes
  std::string from;    // the feed it came from ("GitHub")
};

// This build's version ("2026.10.19"; "dev" for a build of one's own)
const char *version();
bool official();

// Whether this copy can update itself (an official install); why not
bool possible(std::string *why);
// The looking thread: 15 s after start, every 30 min (5 after a failure);
// a newer signed release is downloaded and checked in the background
void start();
// Look now (Game Options' Check now), even with Check for updates off
void checkNow();
enum class State { Off, Idle, Checking, Downloading, Ready, UpToDate, Failed };
State state();
std::string status();
// A downloaded, checked release to offer, once (Check now offers it again)
bool takeOffer(Release &out);
// The player said not to be asked about this one again
void skip(const Release &r);
// Run the ready installer now (relaunching the game): quit after
bool installNow(std::string &why);
// The installer was started: the game should close now
bool installing();
// The game closing with an update ready (Later): installed quietly
void atExit();
// zt1-engine.exe --check-update [--install]: for trying the updater out
int checkNowCli(bool installIt);

} // namespace Updater

#endif // UPDATER_HPP
