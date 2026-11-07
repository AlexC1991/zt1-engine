#include "ModMenu.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <string>
#include <vector>

#include "Embed.hpp"
#include "IniReader.hpp"
#include "ModImport.hpp"
#include "ModRepo.hpp"
#include "Mods.hpp"
#include "Paths.hpp"
#include "Features.hpp"
#include "ResourceManager.hpp"
#include "Settings.hpp"
#include "ui/UiAction.hpp"
#include "ui/UiButton.hpp"
#include "ui/UiImage.hpp"
#include "ui/UiLayout.hpp"
#include "ui/UiListBox.hpp"
#include "ui/UiText.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#endif

namespace {
// The Tutorial & Scenario screen's ids (ui/scenario.lyt), used as the Mod
// Menu's: its picture, list, story (the mod's details) and objectives (what
// it changes; in the repository, its parts)
constexpr int kList = 50002, kAbout = 50004, kArt = 50001;
constexpr int kListLabel = 14101, kAboutLabel = 14102, kDetails = 50006, kDetailsLabel = 14103;
constexpr int kToggle = 50008; // its Start button: Switch on / off (Download)
// Ours, in the same style: the folder, its button, Restart, a status line,
// and the mod repository's button (Your mods / Mod repository)
constexpr int kFolderText = 7142, kFolderButton = 7141, kRestart = 7143, kRepository = 7144, kAdd = 7145, kSort = 7146;

// What the window adds to the scenario layout: the original's own fonts,
// colours and button frame (ui/gameopts/textbck) - the folder under the list
// (shortened to make room), Folder... and Mod repository beside Back, and
// Restart between Back and Switch on
const char *kExtra = R"(
[Mods folder]
type=UIText
id=7142
x=20
y=537
dx=336
dy=20
font=11520
fontsize=11521
border=0
forecolor=255
forecolor=212
forecolor=60
backcolor=68
backcolor=36
backcolor=20
transparent=1
layer=1
justify=left

[Mods folder button]
type=UIButton
id=7141
x=20
y=560
justify=center
font = 14000
fontsize = 14001
forecolor = 255
forecolor = 186
forecolor = 16
backcolor = 131
backcolor = 125
backcolor = 53
highlightcolor = 255
highlightcolor = 223
highlightcolor = 41
selectcolor = 90
selectcolor = 255
selectcolor = 255
graycolor = 132
graycolor = 149
graycolor = 124
animation = ui/gameopts/textbck/textbck
transparent = 0
layer = 2

[Restart to load]
type=UIButton
id=7143
x=525
y=560
justify=center
font = 14000
fontsize = 14001
forecolor = 255
forecolor = 186
forecolor = 16
backcolor = 131
backcolor = 125
backcolor = 53
highlightcolor = 255
highlightcolor = 223
highlightcolor = 41
selectcolor = 90
selectcolor = 255
selectcolor = 255
graycolor = 132
graycolor = 149
graycolor = 124
animation = ui/gameopts/textbck/textbck
transparent = 0
layer = 2

[Sort the list]
type=UIButton
id=7146
x=525
y=560
justify=center
font = 14000
fontsize = 14001
forecolor = 255
forecolor = 186
forecolor = 16
backcolor = 131
backcolor = 125
backcolor = 53
highlightcolor = 255
highlightcolor = 223
highlightcolor = 41
selectcolor = 90
selectcolor = 255
selectcolor = 255
graycolor = 132
graycolor = 149
graycolor = 124
animation = ui/gameopts/textbck/textbck
transparent = 0
layer = 2

[Add a mod]
type=UIButton
id=7145
x=268
y=560
justify=center
font = 14000
fontsize = 14001
forecolor = 255
forecolor = 186
forecolor = 16
backcolor = 131
backcolor = 125
backcolor = 53
highlightcolor = 255
highlightcolor = 223
highlightcolor = 41
selectcolor = 90
selectcolor = 255
selectcolor = 255
graycolor = 132
graycolor = 149
graycolor = 124
animation = ui/gameopts/textbck/textbck
transparent = 0
layer = 2

[Mod repository]
type=UIButton
id=7144
x=150
y=560
justify=center
font = 14000
fontsize = 14001
forecolor = 255
forecolor = 186
forecolor = 16
backcolor = 131
backcolor = 125
backcolor = 53
highlightcolor = 255
highlightcolor = 223
highlightcolor = 41
selectcolor = 90
selectcolor = 255
selectcolor = 255
graycolor = 132
graycolor = 149
graycolor = 124
animation = ui/gameopts/textbck/textbck
transparent = 0
layer = 2
)";

// The mod repository's view (ModRepo.hpp): its list is each mod's parts
bool g_repo = false;
struct RepoRow {
  int mod, part; // (part -1: the heading of a mod in parts)
};
std::vector<ModRepo::Mod> g_repoMods;
std::vector<RepoRow> g_repoRows;
ModRepo::State g_repoShown = ModRepo::State::Unavailable;
int g_repoPercent = -1;

// ours: a mod's name for its row in the lists - without words in brackets
// ("ZT1 HD Art (AI upscaled): Zoo Tycoon" is "ZT1 HD Art: Zoo Tycoon"), so
// the part and the size fit; the whole name is in About it
std::string rowName(const std::string &name) {
  std::string out;
  int depth = 0;
  for (char c : name) {
    if (c == '(')
      depth++;
    else if (c == ')' && depth > 0)
      depth--;
    else if (depth == 0)
      out += c;
  }
  std::string tidy;
  for (char c : out)
    if (!(c == ' ' && (tidy.empty() || tidy.back() == ' ')))
      tidy += c;
  for (size_t at; (at = tidy.find(" :")) != std::string::npos;)
    tidy.erase(at, 1);
  while (!tidy.empty() && tidy.back() == ' ')
    tidy.pop_back();
  return tidy.empty() ? name : tidy;
}

// ours: a mod's first part not in your mods at its version (-1: you have them all)
int nextPart(const ModRepo::Mod &m) {
  for (int p = 0; p < static_cast<int>(m.parts.size()); p++)
    if (ModRepo::installedVersion(m, m.parts[p]) != m.version)
      return p;
  return -1;
}

std::string sizeText(long long bytes) {
  char b[32];
  if (bytes >= 1024LL * 1024 * 1024)
    std::snprintf(b, sizeof b, "%.1f GB", bytes / (1024.0 * 1024 * 1024));
  else if (bytes >= 1024LL * 1024)
    std::snprintf(b, sizeof b, "%.1f MB", bytes / (1024.0 * 1024));
  else
    std::snprintf(b, sizeof b, "%.0f KB", bytes / 1024.0);
  return b;
}

// The folders switched on when the game started (what's loaded now)
std::set<std::string> g_loaded;
bool g_loadedKnown = false;

std::vector<Mods::Mod> g_mods;

// The cheats, listed above your mods (switched like one): each a row
constexpr int kCheatRows = 1; // Infinite money (Features::infiniteMoney)

template <class T> T *el(UiLayout *l, int id) { return l ? dynamic_cast<T *>(l->getElementById(id)) : nullptr; }

void setText(UiLayout *l, int id, const std::string &s) {
  if (UiText *t = el<UiText>(l, id))
    t->setText(s);
}
void setLabel(UiLayout *l, int id, const std::string &s) {
  if (UiButton *b = el<UiButton>(l, id))
    b->setLabel(s);
}

std::string shortPath(const std::string &p, size_t n) {
  return p.size() <= n ? p : "..." + p.substr(p.size() - (n - 3));
}

void repositorySelection(UiLayout *layout);

namespace fs = std::filesystem;

// ours: the list's order, picked with Sort (your mods; the repository's)
int g_sortMine = 0, g_sortRepo = 0;
const char *const kMineSorts[] = {"Name", "Size", "Switched on", "Newest"};
const char *const kRepoSorts[] = {"Name", "Size", "Not downloaded"};

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// ours: each mod's size on disk, measured in the background (an HD art mod
// is tens of gigabytes in hundreds of archives: the menu doesn't wait)
std::mutex g_sizeLock;
std::map<std::string, long long> g_sizes;
std::set<std::string> g_sizing;
bool g_sizesChanged = false;
long long sizeOf(const std::string &path) {
  {
    std::lock_guard<std::mutex> g(g_sizeLock);
    auto it = g_sizes.find(path);
    if (it != g_sizes.end())
      return it->second;
    if (!g_sizing.insert(path).second)
      return -1;
  }
  std::thread([path] {
    long long total = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
      if (it->is_regular_file(ec))
        total += static_cast<long long>(it->file_size(ec));
    std::lock_guard<std::mutex> g(g_sizeLock);
    g_sizes[path] = total;
    g_sizing.erase(path);
    g_sizesChanged = true;
  }).detach();
  return -1;
}

// ours: the folder line - where mods are, and the room left on its drive
std::string folderLine() {
  std::error_code ec;
  const fs::space_info room = fs::space(Mods::folder(), ec);
  return "Folder: " + shortPath(Mods::folder(), 24) + (ec ? "" : " - " + sizeText(static_cast<long long>(room.available)) + " free");
}

// ours: your mods moved to the folder just picked, in the background - copied
// file by file (a linked folder, as a junction, linked again, its files left
// where they are), the folder switched once all are there, and the old
// copies removed when the game next starts (Mods::cleanupMoved: the game has
// some of their files open now)
std::atomic<bool> g_moving{false}, g_moveChanged{false};
std::atomic<long long> g_moveDone{0}, g_moveTotal{0};
std::mutex g_moveLock;
std::string g_moveStatus; // (under g_moveLock)

bool linkedFolder(const fs::path &p) {
#ifdef _WIN32
  const DWORD a = GetFileAttributesW(p.wstring().c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
  std::error_code ec;
  return fs::is_symlink(p, ec);
#endif
}

void moveMods(const std::string &from, const std::string &to) {
  g_moving = true;
  g_moveDone = 0;
  g_moveTotal = 0;
  std::thread([from, to] {
    std::error_code ec;
    std::vector<fs::path> mods;
    for (const fs::directory_entry &d : fs::directory_iterator(from, ec)) {
      const std::string name = d.path().filename().string();
      if (!name.empty() && name[0] != '.' && d.is_directory(ec))
        mods.push_back(d.path());
    }
    long long total = 0;
    for (const fs::path &m : mods)
      if (!linkedFolder(m))
        for (auto it = fs::recursive_directory_iterator(m, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
          if (it->is_regular_file(ec))
            total += static_cast<long long>(it->file_size(ec));
    g_moveTotal = total;
    std::string problem;
    std::vector<std::string> moved;
    for (const fs::path &m : mods) {
      const fs::path target = fs::path(to) / m.filename();
      if (linkedFolder(m)) {
#ifdef _WIN32
        const fs::path points = fs::read_symlink(m, ec);
        if (ec || points.empty()) {
          problem = "Couldn't read where " + m.filename().string() + " links to.";
          break;
        }
        std::string cmd = "cmd /c mklink /J \"" + target.string() + "\" \"" + points.string() + "\" >nul";
        if (!fs::exists(target, ec) && std::system(cmd.c_str()) != 0) {
          problem = "Couldn't link " + m.filename().string() + " in the new folder.";
          break;
        }
#endif
        moved.push_back(m.string());
        continue;
      }
      for (auto it = fs::recursive_directory_iterator(m, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), m, ec), out = target / rel;
        if (it->is_directory(ec)) {
          fs::create_directories(out, ec);
          continue;
        }
        fs::create_directories(out.parent_path(), ec);
        const long long bytes = static_cast<long long>(it->file_size(ec));
        if (!fs::copy_file(it->path(), out, fs::copy_options::overwrite_existing, ec) || ec) {
          problem = "Couldn't copy " + rel.string() + " (" + ec.message() + ").";
          break;
        }
        g_moveDone += bytes;
      }
      if (!problem.empty() || ec) {
        if (problem.empty())
          problem = "Couldn't read " + m.filename().string() + " (" + ec.message() + ").";
        break;
      }
      moved.push_back(m.string());
    }
    std::lock_guard<std::mutex> g(g_moveLock);
    if (problem.empty()) {
      Mods::setFolder(to);
      // (the old copies, for Mods::cleanupMoved at the next start)
      std::ofstream list(fs::path(Paths::dataDir()) / "mods-cleanup.txt", std::ios::app);
      for (const std::string &m : moved)
        list << m << "\n";
      g_moveStatus = "Moved " + std::to_string(moved.size()) + " mods to " + to +
                     ". The old copies are removed the next time the game starts.";
    } else {
      g_moveStatus = "Your mods weren't moved: " + problem + " The mods folder is as it was.";
    }
    g_moving = false;
    g_moveChanged = true;
  }).detach();
}

std::string moveText() {
  if (g_moving) {
    const long long total = g_moveTotal, done = g_moveDone;
    return "Moving your mods to the new folder: " + sizeText(done) + (total > 0 ? " of " + sizeText(total) : "") +
           ". Keep the game open until it's done.";
  }
  std::lock_guard<std::mutex> g(g_moveLock);
  return g_moveStatus;
}

// The objectives' list (its label and lines): what a mod changes, or a
// repository mod's parts
void setDetails(UiLayout *layout, const std::string &label, const std::vector<std::string> &lines) {
  setText(layout, kDetailsLabel, label);
  if (UiListBox *d = el<UiListBox>(layout, kDetails)) {
    d->clear();
    for (const std::string &l : lines)
      d->addItem(l, l);
  }
}

// The repository's view: each mod a row - one in parts its own heading,
// each part a row under it - with whether it's in your mods already
void repositoryRefresh(UiLayout *layout) {
  UiListBox *list = el<UiListBox>(layout, kList);
  // (the picked row kept by what it is: sorting moves rows)
  const std::string keepKey = list ? list->getSelectedData() : "";
  g_repoMods = ModRepo::mods();
  g_repoRows.clear();
  if (list)
    list->clear();
  // (which parts you have, looked up once: it checks their files)
  std::map<std::pair<int, int>, std::string> had;
  for (int m = 0; m < static_cast<int>(g_repoMods.size()); m++)
    for (int p = 0; p < static_cast<int>(g_repoMods[m].parts.size()); p++)
      had[{m, p}] = ModRepo::installedVersion(g_repoMods[m], g_repoMods[m].parts[p]);
  auto total = [](int m) {
    long long t = 0;
    for (const ModRepo::Part &p : g_repoMods[m].parts)
      t += p.size;
    return t;
  };
  auto missing = [&](int m) {
    int n = 0;
    for (int p = 0; p < static_cast<int>(g_repoMods[m].parts.size()); p++)
      n += had[{m, p}].empty();
    return n;
  };
  // (ours: in the order Sort says - name, size, or not downloaded first -
  // the mods, then the parts under each the same way; a mod's parts stay
  // together under its heading)
  std::vector<int> order;
  for (int m = 0; m < static_cast<int>(g_repoMods.size()); m++)
    order.push_back(m);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    if (g_sortRepo == 1 && total(a) != total(b))
      return total(a) > total(b);
    if (g_sortRepo == 2 && (missing(a) > 0) != (missing(b) > 0))
      return missing(a) > 0;
    return lower(g_repoMods[a].name) < lower(g_repoMods[b].name);
  });
  for (int m : order) {
    std::vector<int> parts;
    for (int p = 0; p < static_cast<int>(g_repoMods[m].parts.size()); p++)
      parts.push_back(p);
    std::stable_sort(parts.begin(), parts.end(), [&](int a, int b) {
      if (g_sortRepo == 1)
        return g_repoMods[m].parts[a].size > g_repoMods[m].parts[b].size;
      if (g_sortRepo == 2)
        return had[{m, a}].empty() && !had[{m, b}].empty();
      return false; // (by name: the mod's own order)
    });
    if (parts.size() > 1)
      g_repoRows.push_back({m, -1});
    for (int p : parts)
      g_repoRows.push_back({m, p});
  }
  const std::string downloading = ModRepo::downloading();
  int select = 0;
  for (int i = 0; i < static_cast<int>(g_repoRows.size()); i++) {
    const int m = g_repoRows[i].mod, p = g_repoRows[i].part;
    const ModRepo::Mod &mod = g_repoMods[m];
    std::string key, text, size;
    if (p < 0) {
      // (the heading: the mod, all its parts' size, and how many you have)
      key = mod.id + "/";
      const int n = static_cast<int>(mod.parts.size()), have = n - missing(m);
      text = (have == n ? "[Have]  " : "") + rowName(mod.name);
      size = (have > 0 && have < n ? std::to_string(have) + " of " + std::to_string(n) + " - " : std::string()) +
             sizeText(total(m));
    } else {
      const ModRepo::Part &part = mod.parts[p];
      key = mod.id + "/" + part.id;
      const std::string &v = had[{m, p}];
      std::string mark = v.empty() ? "" : v == mod.version ? "[Have]  " : "[Update]  ";
      size = sizeText(part.size);
      if (key == downloading) {
        mark = "[Downloading]  ";
        size = std::to_string(static_cast<int>(ModRepo::progress() * 100)) + "% of " + size;
      }
      text = mod.parts.size() > 1 ? "      " + mark + part.name : mark + rowName(mod.name);
    }
    if (list) {
      list->addItem(text, key);
      list->setItemRight(i, size);
    }
    if (key == keepKey)
      select = i;
  }
  if (list && !g_repoRows.empty())
    list->setSelectedIndex(select);
  setText(layout, kFolderText, folderLine());
  setLabel(layout, kSort, std::string("Sort: ") + kRepoSorts[g_sortRepo]);
  repositorySelection(layout);
}

void repositorySelection(UiLayout *layout) {
  if (UiImage *art = el<UiImage>(layout, kArt))
    art->setHidden(true);
  ModRepo::State s = ModRepo::state();
  std::string status = ModRepo::status();
  if (s == ModRepo::State::Installing) {
    const long long done = ModRepo::downloadedBytes(), total = ModRepo::downloadTotal();
    status = "Downloading: " + (total > 0 ? sizeText(done) + " of " + sizeText(total) + " - " : std::string()) +
             std::to_string(static_cast<int>(ModRepo::progress() * 100)) + "%";
  }
  const std::string moving = moveText();
  if (!moving.empty())
    status = status.empty() ? moving : status + "\n\n" + moving;
  UiListBox *list = el<UiListBox>(layout, kList);
  int i = list ? list->getSelectedIndex() : -1;
  if (i < 0 || i >= static_cast<int>(g_repoRows.size())) {
    setText(layout, kAbout, status.empty() ? "The repository has nothing yet." : status);
    setLabel(layout, kToggle, "Download");
    setDetails(layout, "Its parts", {});
    return;
  }
  const ModRepo::Mod &m = g_repoMods[g_repoRows[i].mod];
  if (!m.picture.empty()) {
    if (UiImage *art = el<UiImage>(layout, kArt)) {
      art->setPngImage(m.picture);
      art->setHidden(false);
    }
  }
  std::string about = m.name + (m.version.empty() ? "" : "  (version " + m.version + ")");
  if (!m.author.empty())
    about += "\nby " + m.author;
  if (g_repoRows[i].part < 0) {
    // (a mod's heading: all its parts; Download gets the next one you haven't)
    long long total = 0;
    int have = 0;
    for (const ModRepo::Part &q : m.parts) {
      total += q.size;
      have += ModRepo::installedVersion(m, q) == m.version;
    }
    const int next = nextPart(m);
    about += "\n\n" + std::to_string(m.parts.size()) + " parts, " + sizeText(total) + " in all. You have " +
             std::to_string(have) + " of them.";
    if (next >= 0)
      about += "\nDownload next: " + m.parts[next].name + " (" + sizeText(m.parts[next].size) + ").";
    setLabel(layout, kToggle, next >= 0 ? "Download next" : "In your mods");
  } else {
    const ModRepo::Part &p = m.parts[g_repoRows[i].part];
    std::string have = ModRepo::installedVersion(m, p);
    about += "\n\n" + p.name + ": " + sizeText(p.size) + (p.files ? ", " + std::to_string(p.files) + " files" : "");
    about += have.empty() ? "\nNot in your mods yet." : have == m.version ? "\nIn your mods." : "\nIn your mods: an older version (" + have + ").";
    setLabel(layout, kToggle, have.empty() ? "Download" : have == m.version ? "Download again" : "Update");
  }
  if (!status.empty())
    about += "\n\n" + status;
  if (!m.about.empty())
    about += "\n\n" + m.about;
  if (!m.notice.empty())
    about += "\n\n" + m.notice;
  setText(layout, kAbout, about);
  // (each of its parts, and which you have)
  std::vector<std::string> parts;
  for (const ModRepo::Part &q : m.parts) {
    std::string v = ModRepo::installedVersion(m, q);
    parts.push_back(q.name + "  " + sizeText(q.size) + (v.empty() ? "" : v == m.version ? "  - have it" : "  - older"));
  }
  setDetails(layout, "Its parts", parts);
}
} // namespace

namespace ModMenu {
bool changed() {
  std::set<std::string> now;
  for (const Mods::Mod &m : Mods::list())
    if (m.enabled)
      now.insert(m.folder);
  const auto loaded = Mods::appliedFolders();
  return now != std::set<std::string>(loaded.begin(), loaded.end());
}

UiLayout *open(ResourceManager *rm, const std::string &layoutPath) {
  if (!g_loadedKnown) {
    for (const Mods::Mod &m : Mods::list())
      if (m.enabled)
        g_loaded.insert(m.folder);
    g_loadedKnown = true;
  }
  int size = 0;
  void *data = rm->getFileContent(layoutPath, &size);
  std::string text = data ? std::string(static_cast<char *>(data), size) : std::string();
  free(data);
  // (the list a little shorter, for the folder line under it)
  size_t list = text.find("[ScenarioList]");
  if (list != std::string::npos) {
    size_t next = text.find("\n[", list + 1), dy = text.find("dy=240", list);
    if (dy != std::string::npos && (next == std::string::npos || dy < next))
      text.replace(dy, 6, "dy=196");
  }
  text += kExtra;
  IniReader *ini = new IniReader(text.data(), text.size());
  UiLayout *layout = new UiLayout(ini, rm);
  setText(layout, kListLabel, "Mods");
  setText(layout, kAboutLabel, "About it");
  setText(layout, kDetailsLabel, "It changes");
  setLabel(layout, kFolderButton, "Folder...");
  if (UiButton *b = el<UiButton>(layout, kRestart)) b->setHidden(true); // switches apply automatically
  g_repo = false;
  setLabel(layout, kRepository, "Mod repository");
  // (only the official installs carry the repository's client, modrepo.dll:
  // the Game Build's Mod Menu is the player's own mods alone)
  if (UiButton *b = el<UiButton>(layout, kRepository))
    b->setHidden(!ModRepo::available());
  setLabel(layout, kAdd, "Add a mod...");
  setLabel(layout, kSort, "Sort: Name");
  if (UiListBox *list = el<UiListBox>(layout, kList))
    list->setSelectionAction(UiAction::MOD_LIST_SELECTION);
  refresh(layout, rm);
  return layout;
}

void refresh(UiLayout *layout, ResourceManager *rm) {
  const auto loaded = Mods::appliedFolders();
  g_loaded = std::set<std::string>(loaded.begin(), loaded.end());
  if (g_repo)
    return repositoryRefresh(layout);
  UiListBox *list = el<UiListBox>(layout, kList);
  int keep = list ? list->getSelectedIndex() : -1;
  // (the picked row kept by what it is: sorting moves rows)
  const std::string keepKey = list ? list->getSelectedData() : "";
  g_mods = Mods::list();
  // (ours: in the order Sort says - name, size, switched on first, newest)
  std::map<std::string, long long> sizes, newest;
  for (const Mods::Mod &m : g_mods) {
    sizes[m.folder] = sizeOf(m.path);
    std::error_code ec;
    newest[m.folder] = static_cast<long long>(fs::last_write_time(m.path, ec).time_since_epoch().count());
  }
  std::stable_sort(g_mods.begin(), g_mods.end(), [&](const Mods::Mod &a, const Mods::Mod &b) {
    if (g_sortMine == 1 && sizes[a.folder] != sizes[b.folder]) return sizes[a.folder] > sizes[b.folder];
    if (g_sortMine == 2 && a.enabled != b.enabled) return a.enabled;
    if (g_sortMine == 3 && newest[a.folder] != newest[b.folder]) return newest[a.folder] > newest[b.folder];
    return lower(a.name) < lower(b.name);
  });
  if (list) {
    list->clear();
    list->addItem(std::string(Features::infiniteMoney ? "[On]  " : "[Off]  ") + "Cheat: infinite money", "*infinitemoney");
    int select = -1;
    for (int i = 0; i < static_cast<int>(g_mods.size()); i++) {
      const Mods::Mod &m = g_mods[i];
      const long long bytes = sizes[m.folder];
      list->addItem((m.enabled ? "[On]  " : "[Off]  ") + rowName(m.name), m.folder);
      if (bytes >= 0)
        list->setItemRight(kCheatRows + i, sizeText(bytes));
      if (m.folder == keepKey)
        select = kCheatRows + i;
    }
    const int rows = kCheatRows + static_cast<int>(g_mods.size());
    list->setSelectedIndex(select >= 0 ? select : keep >= 0 && keep < rows ? keep : g_mods.empty() ? 0 : kCheatRows);
  }
  setText(layout, kFolderText, folderLine());
  setLabel(layout, kSort, std::string("Sort: ") + kMineSorts[g_sortMine]);
  if (g_mods.empty()) {
    setText(layout, kAbout,
            "No mods in this folder yet.\n\nA mod is a folder with a mod.json and its files (a .ztd), "
            "made in the ZT1 Lab or by hand. Put one in the mods folder, or choose another folder.");
    setLabel(layout, kToggle, "Enable");
    setDetails(layout, "It changes", {});
  }
  selectionChanged(layout, rm);
}

void selectionChanged(UiLayout *layout, ResourceManager *rm) {
  if (g_repo)
    return repositorySelection(layout);
  UiListBox *list = el<UiListBox>(layout, kList);
  int i = list ? list->getSelectedIndex() : -1;
  if (i == 0) {
    // (the cheat: no picture, its own words)
    if (UiImage *art = el<UiImage>(layout, kArt))
      art->setHidden(true);
    if (UiButton *b = el<UiButton>(layout, kToggle))
      b->setDisabled(false);
    std::string about = std::string("Cheat: infinite money\n\n") + (Features::infiniteMoney ? "Switched on" : "Switched off") +
                        "\n\nBuy anything, whatever your cash: spending still shows in the zoo's finances, but your cash "
                        "never goes down, and research, conservation and marketing are always paid for. Not part of the "
                        "original game. It works straight away, in the game you're playing too.";
    if (g_mods.empty())
      about += "\n\nNo mods in this folder yet: put one in the mods folder, or choose another folder.";
    setText(layout, kAbout, about);
    setLabel(layout, kToggle, Features::infiniteMoney ? "Disable" : "Enable");
    setDetails(layout, "It changes", {"Purchases: never refused for lack of money", "Cash: never goes down",
                                      "Funding: always paid"});
    return;
  }
  i -= kCheatRows;
  if (i < 0 || i >= static_cast<int>(g_mods.size()))
    return;
  const Mods::Mod &m = g_mods[i];
  std::string about = m.name;
  if (!m.version.empty())
    about += "  (version " + m.version + ")";
  if (!m.author.empty())
    about += "\nby " + m.author;
  about += std::string("\n\n") + (m.enabled ? "Switched on" : "Switched off");
  const long long bytes = sizeOf(m.path);
  if (bytes >= 0)
    about += " - " + sizeText(bytes) + " on disk";
  const std::string moving = moveText();
  if (!moving.empty())
    about += "\n\n" + moving;
  const std::string required = m.enabled ? Mods::offReason(m.folder) : "";
  if (!required.empty()) about += "\n\n" + required;
  if (UiButton *b = el<UiButton>(layout, kToggle)) b->setDisabled(!required.empty());
  if (!m.about.empty())
    about += "\n\n" + m.about;
  setText(layout, kAbout, about);
  setLabel(layout, kToggle, !required.empty() ? "Required" : m.enabled ? "Disable" : "Enable");
  // What it changes, in the objectives' list
  std::vector<std::string> lines = m.replaces;
  if (lines.empty())
    lines.push_back(m.settings.empty() ? "(it doesn't say)" : "Its own settings: Game Options > Mods & Updates");
  setDetails(layout, "It changes", lines);
  // Its picture: its own (mod.json "picture"), else the first frame it
  // changes (as loaded now: the mod's own when it's on), with the palette it
  // names
  if (UiImage *art = el<UiImage>(layout, kArt)) {
    art->setHidden(true);
    if (!m.picture.empty()) {
      art->setFileImage(m.picture);
      art->setHidden(false);
    } else if (!m.preview.empty() && !m.palette.empty() && rm->hasResource(m.preview)) {
      art->setZt1Image(m.preview, g_loaded.count(m.folder) ? m.palette : m.originalPalette);
      art->setHidden(false);
    }
  }
}

void toggle(UiLayout *layout, ResourceManager *rm) {
  if (g_moving)
    return; // (your mods are being moved)
  if (g_repo) {
    // (the repository's view: the picked part downloaded)
    UiListBox *list = el<UiListBox>(layout, kList);
    int i = list ? list->getSelectedIndex() : -1;
    if (i >= 0 && i < static_cast<int>(g_repoRows.size())) {
      const ModRepo::Mod &m = g_repoMods[g_repoRows[i].mod];
      // (a mod's heading: its next part you haven't got)
      const int p = g_repoRows[i].part >= 0 ? g_repoRows[i].part : nextPart(m);
      if (p < 0)
        return;
      const ModRepo::Part &part = m.parts[p];
      // ours: room for it first - the download and the unpacked part both,
      // for a while - rather than a full drive halfway through
      std::error_code ec;
      const fs::space_info room = fs::space(Mods::folder(), ec);
      const long long needs = part.size * 2 + 256LL * 1024 * 1024;
      if (!ec && static_cast<long long>(room.available) < needs) {
        setText(layout, kAbout, "Not enough room for " + m.name + ": it needs about " + sizeText(needs) + " free while it downloads, and the mods folder's drive has " +
                                    sizeText(static_cast<long long>(room.available)) + ".\n\nPick a mods folder on a drive with more room: Folder...");
        return;
      }
      ModRepo::install(m.id, part.id);
      tick(layout, rm);
    }
    return;
  }
  UiListBox *list = el<UiListBox>(layout, kList);
  int i = list ? list->getSelectedIndex() : -1;
  if (i == 0) {
    Features::infiniteMoney = !Features::infiniteMoney;
    Settings::saveExtra("infinitemoney", Features::infiniteMoney); // (Game Options' switch too)
    refresh(layout, rm);
    return;
  }
  i -= kCheatRows;
  if (i < 0 || i >= static_cast<int>(g_mods.size()))
    return;
  Mods::setEnabled(g_mods[i].folder, !g_mods[i].enabled);
  refresh(layout, rm);
}

void chooseFolder(UiLayout *layout, ResourceManager *rm) {
  if (g_moving)
    return;
#ifdef _WIN32
  BROWSEINFOA bi{};
  char name[MAX_PATH] = {};
  bi.pszDisplayName = name;
  bi.lpszTitle = "The folder for your mods - pick one on a drive with room for them";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  std::string picked;
  if (LPITEMIDLIST id = SHBrowseForFolderA(&bi)) {
    char path[MAX_PATH] = {};
    if (SHGetPathFromIDListA(id, path))
      picked = path;
    CoTaskMemFree(id);
  }
  std::error_code ec;
  const std::string old = Mods::folder();
  if (!picked.empty() && !fs::equivalent(picked, old, ec)) {
    // ours: the mods in the old folder - move them there too?
    int count = 0;
    for (const fs::directory_entry &d : fs::directory_iterator(old, ec)) {
      const std::string n = d.path().filename().string();
      if (!n.empty() && n[0] != '.' && d.is_directory(ec))
        ++count;
    }
    int answer = IDNO;
    if (count > 0) {
      const std::string ask = (count == 1 ? std::string("Move your mod to ") : "Move your " + std::to_string(count) + " mods to ") + picked + "?\n\n" +
                              "Yes: they're copied there now (you can keep using the menu), and the old copies are removed " +
                              "the next time the game starts.\nNo: use the new folder; your mods stay in " + old + ".";
      answer = MessageBoxA(nullptr, ask.c_str(), "Mods folder", MB_YESNOCANCEL | MB_ICONQUESTION);
    }
    if (answer == IDYES)
      moveMods(old, picked);
    else if (answer == IDNO)
      Mods::setFolder(picked);
  }
#endif
  refresh(layout, rm);
}

void sort(UiLayout *layout, ResourceManager *rm) {
  if (g_repo)
    g_sortRepo = (g_sortRepo + 1) % 3;
  else
    g_sortMine = (g_sortMine + 1) % 4;
  refresh(layout, rm);
}

void addMod(UiLayout *layout, ResourceManager *rm) {
#ifdef _WIN32
  char file[MAX_PATH * 4] = {};
  OPENFILENAMEA of{};
  of.lStructSize = sizeof of;
  of.lpstrFilter = "Mods (.ztd, .zip, mod.json)\0*.ztd;*.zip;mod.json\0All files\0*.*\0";
  of.lpstrFile = file;
  of.nMaxFile = sizeof file;
  of.lpstrTitle = "Add a mod: an old Zoo Tycoon download (.ztd), a .zip, or a mod's mod.json";
  of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameA(&of))
    return;
  ModImport::Result r = ModImport::import(file);
  if (g_repo)
    repository(layout, rm); // (back to your mods, to see it)
  refresh(layout, rm);
  // (the one just added picked)
  if (r.ok)
    if (UiListBox *list = el<UiListBox>(layout, kList))
      for (int i = 0; i < static_cast<int>(g_mods.size()); i++)
        if (g_mods[i].folder == r.folder) {
          list->setSelectedIndex(kCheatRows + i);
          selectionChanged(layout, rm);
        }
  if (UiText *t = el<UiText>(layout, kAbout))
    t->setText(r.message + "\n\n" + t->getText());
#else
  (void)layout;
  (void)rm;
#endif
}

void repository(UiLayout *layout, ResourceManager *rm) {
  g_repo = !g_repo;
  setLabel(layout, kRepository, g_repo ? "Your mods" : "Mod repository");
  setText(layout, kListLabel, g_repo ? "Mod repository" : "Mods");
  if (g_repo) {
    ModRepo::fetch();
    g_repoShown = ModRepo::state();
    g_repoPercent = -1;
  }
  refresh(layout, rm);
}

void tick(UiLayout *layout, ResourceManager *rm) {
  bool sizes;
  {
    std::lock_guard<std::mutex> g(g_sizeLock);
    sizes = g_sizesChanged;
    g_sizesChanged = false;
  }
  static long long movedShown = -1;
  const bool moveNews = g_moveChanged.exchange(false) || (g_moving && g_moveDone / (64LL << 20) != movedShown);
  if (g_moving)
    movedShown = g_moveDone / (64LL << 20);
  if (!g_repo) {
    if (sizes || moveNews)
      refresh(layout, rm);
    return;
  }
  if (moveNews)
    repositorySelection(layout);
  // (the list again once the catalogue's in or a download's done; the
  // details as a download goes)
  ModRepo::State s = ModRepo::state();
  int percent = s == ModRepo::State::Installing ? static_cast<int>(ModRepo::progress() * 100) : -1;
  if (s != g_repoShown) {
    g_repoShown = s;
    g_repoPercent = percent;
    refresh(layout, rm);
  } else if (percent != g_repoPercent) {
    // (the row's own percentage too)
    g_repoPercent = percent;
    repositoryRefresh(layout);
  }
}

bool restart(UiLayout *layout) {
  if (Embed::active()) {
    setText(layout, kAbout, "In the ZT1 Lab, the game can't restart itself: stop Play and start it again, "
                            "and the mods switched on are loaded.");
    return false;
  }
#ifdef _WIN32
  char exe[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  STARTUPINFOA si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  std::string cmd = GetCommandLineA();
  if (CreateProcessA(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
  }
#endif
  setText(layout, kAbout, "The game couldn't restart itself: close it and start it again.");
  return false;
}
} // namespace ModMenu
