#ifndef UPDATER_HPP
#define UPDATER_HPP

#include <string>
#include <cstdint>

// addition: visible updates for installed, official builds. The feed is
// CodeRook's zt1-engine release (zt1-update.yml), else
// GitHub's latest (latest.yml). Either carries an ECDSA P-256 signature over
// the app, version, installer name, size and SHA-512, verified against the key
// pinned here before anything is downloaded. Checked at startup and on
// Check now (Game Options > Updates). A newer release is offered first;
// accepting it downloads into the data folder's updates\ with real byte
// progress, then checks its size and SHA-512. Explicit installation opens
// the separate updater helper before the game closes. Later never installs
// at exit. The helper rechecks the installer and confirms the new app opens.
//
// Only official builds (made by the release tooling, ZT1_OFFICIAL_BUILD)
// ever update: a copy someone built themselves keeps their own code. The
// "updatechecks" switch (Features.hpp) turns startup checks off (Check now
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
// A test release (ZT1_TEST_BUILD): made to be tried before it's published -
// it never updates, and keeps its own settings, saves and mods
bool testBuild();

// Whether this copy can update itself (an official install); why not
bool possible(std::string *why);
// The worker checks at startup when enabled. Available releases wait for
// explicit downloadNow(); network/disk work never blocks the rendering loop.
void start();
// Look now (Game Options' Check now), even with Check for updates off
void checkNow();
enum class State { Off, Idle, Checking, Available, Downloading, Verifying, Ready, Installing, UpToDate, Failed };
// addition: one coherent view for the compact update panel. Progress comes
// from bytes actually written; verification/installation have no percentage.
struct Snapshot {
  State state = State::Off;
  Release release;
  uint64_t downloadedBytes = 0, totalBytes = 0;
  double bytesPerSecond = 0, secondsRemaining = -1;
  std::string status;
};
Snapshot snapshot();
// Download only after the player accepts the signed release offer.
bool downloadNow();
State state();
std::string status();
// An available or downloaded release to offer once (Check now offers again).
bool takeOffer(Release &out);
// The player said not to be asked about this one again
void skip(const Release &r);
// Run the ready installer now (relaunching the game): quit after
bool installNow(std::string &why);
// The installer was started: the game should close now
bool installing();
// Kept for existing shutdown callers; closing/Later never installs.
void atExit();
// The new app acknowledges the helper only after its first ready frame.
// Accepts only a nonce-named acknowledgement inside our update directory.
void acknowledgeLaunch(int argc, char **argv);
// zt1-engine.exe --check-update [--install]: for trying the updater out
int checkNowCli(bool installIt);

} // namespace Updater

#endif // UPDATER_HPP
