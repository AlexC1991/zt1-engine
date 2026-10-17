#ifndef UPDATER_HPP
#define UPDATER_HPP

#include <string>

// ours: updates for installed, official builds. At start it looks, in the
// background, at GitHub's latest published release (CodeRook mirrors the
// releases, but its feed isn't relied on for updates yet). latest.yml carries
// an ECDSA P-256 signature over the version, installer name, size and
// SHA-512. The updater verifies that signature before offering an update, then
// checks the downloaded installer's size and SHA-512 before running it.
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
  std::string signature; // base64 ECDSA P-256 signature (IEEE P1363)
  std::string size;      // signed installer size in bytes
  std::string from;    // the feed it came from ("GitHub")
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
