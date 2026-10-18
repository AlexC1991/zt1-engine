#ifndef MOD_REPO_HPP
#define MOD_REPO_HPP

#include <string>
#include <vector>

// The mod repository (an addition): mods offered by the official game's
// private mod repository, listed and downloaded from the Mod Menu. Its client
// is modrepo.dll, which only the official install has (setup.exe puts it, and
// the install's own key, beside the exe); it isn't part of the open-source
// engine. A build of one's own has no repository - the Mod Menu says so.
//
// What the engine asks the DLL (C exports, UTF-8 strings):
//   int zt1mr_abi()                                  1
//   int zt1mr_catalogue(char *out, int cap)          the catalogue, checked,
//       as JSON: its length, -(the length needed) when cap is too small, or
//       -1 (zt1mr_error says why)
//   int zt1mr_install(const char *mod, const char *part, const char *modsFolder,
//                     void (*progress)(void *ctx, long long done, long long total),
//                     void *ctx)                     0 once the part is in
//       <modsFolder>\<the mod's folder>, which it leaves switched off; and
//       <that folder>\.modrepo\<part>.json says which version is in
//   const char *zt1mr_error()                        the last failure, in words
//
// The catalogue's JSON: {"mods": [{"id", "name", "author", "about",
// "notice", "version", "folder", "parts": [{"id", "name", "size", "files"}]}]}
namespace ModRepo {

struct Part {
  std::string id, name;
  long long size = 0, files = 0;
};
struct Mod {
  std::string id, name, author, about, notice, version, folder;
  std::vector<Part> parts;
};

enum class State { Unavailable, Idle, Fetching, Ready, Failed, Installing };

// Whether this install has the repository's client (the official install)
bool available();
// The catalogue asked for again, in the background
void fetch();
State state();
// What's happening, or why not, in words
std::string status();
// The catalogue as last fetched
std::vector<Mod> mods();
// A part downloaded and put in the mods folder, in the background; progress
// 0-1 while it goes
void install(const std::string &mod, const std::string &part);
double progress();
// The version of a part already in the mods folder ("" if it isn't)
std::string installedVersion(const Mod &mod, const Part &part);

} // namespace ModRepo

#endif // MOD_REPO_HPP
