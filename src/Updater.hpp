#ifndef UPDATER_HPP
#define UPDATER_HPP

#include <string>

// ours: updates for installed, official builds. At start it looks, in the
// background, at the release feed - CodeRook's zt1-engine-releases first,
// GitHub's latest release if CodeRook can't be reached. Both serve the same
// latest.yml (the electron-updater format: the version, the installer's
// file name, its SHA-512). When there's a newer release, the main menu asks;
// yes downloads the installer, checks its SHA-512, starts it silently and
// quits - the installer puts the new engine in and starts it again.
//
// Only official builds (made by the release workflow, ZT1_OFFICIAL_BUILD)
// ever update: a copy someone built themselves keeps their own code. The
// "updatechecks" switch (Features.hpp) turns the looking off. Never in a
// portable copy (the checkout, the Lab, a zip): nothing there was installed.
namespace Updater {

struct Release {
  std::string version; // "2026.10.19"
  std::string file;    // the installer's name in the feed
  std::string url;     // where to download it
  std::string sha512;  // base64, as the feed gives it
  std::string from;    // "CodeRook" or "GitHub"
};

// This build's version ("2026.10.19"; "dev" for a build of one's own)
const char *version();
bool official();

// Look for a newer release in the background (does nothing for a build of
// one's own, a portable copy, or with the switch off)
void checkInBackground();
// A newer release the background look found, once - and not one the player
// said to skip
bool takeOffer(Release &out);
// The player said not to be asked about this one again
void skip(const Release &r);
// Download, check and start the installer. True: it's running - quit now so
// it can replace the engine
bool install(const Release &r, std::string &why);
// zt1-engine.exe --check-update [--install]: the feed asked now, and what it
// offers said (and installed with --install), whatever the build - for
// trying the updater out. 0 when it went as it should
int checkNow(bool installIt);

} // namespace Updater

#endif // UPDATER_HPP
