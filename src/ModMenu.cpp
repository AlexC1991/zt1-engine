#include "ModMenu.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "Embed.hpp"
#include "IniReader.hpp"
#include "ModImport.hpp"
#include "ModRepo.hpp"
#include "Mods.hpp"
#include "ResourceManager.hpp"
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
constexpr int kFolderText = 7142, kFolderButton = 7141, kRestart = 7143, kRepository = 7144, kAdd = 7145;

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
  int mod, part;
};
std::vector<ModRepo::Mod> g_repoMods;
std::vector<RepoRow> g_repoRows;
ModRepo::State g_repoShown = ModRepo::State::Unavailable;
int g_repoPercent = -1;

std::string sizeText(long long bytes) {
  char b[32];
  if (bytes >= 1024LL * 1024 * 1024)
    std::snprintf(b, sizeof b, "%.1f GB", bytes / (1024.0 * 1024 * 1024));
  else
    std::snprintf(b, sizeof b, "%.0f MB", bytes / (1024.0 * 1024));
  return b;
}

// The folders switched on when the game started (what's loaded now)
std::set<std::string> g_loaded;
bool g_loadedKnown = false;

std::vector<Mods::Mod> g_mods;

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

// The repository's view: each part of each mod a row, with whether it's in
// your mods already
void repositoryRefresh(UiLayout *layout) {
  UiListBox *list = el<UiListBox>(layout, kList);
  int keep = list ? list->getSelectedIndex() : -1;
  g_repoMods = ModRepo::mods();
  g_repoRows.clear();
  if (list)
    list->clear();
  for (int m = 0; m < static_cast<int>(g_repoMods.size()); m++)
    for (int p = 0; p < static_cast<int>(g_repoMods[m].parts.size()); p++) {
      const ModRepo::Mod &mod = g_repoMods[m];
      const ModRepo::Part &part = mod.parts[p];
      std::string have = ModRepo::installedVersion(mod, part);
      std::string mark = have.empty() ? "" : have == mod.version ? "[Have]  " : "[Update]  ";
      std::string name = mod.parts.size() > 1 ? mod.name + ": " + part.name : mod.name;
      if (list)
        list->addItem(mark + name + "  (" + sizeText(part.size) + ")", mod.id + "/" + part.id);
      g_repoRows.push_back({m, p});
    }
  if (list && !g_repoRows.empty())
    list->setSelectedIndex(keep >= 0 && keep < static_cast<int>(g_repoRows.size()) ? keep : 0);
  setText(layout, kFolderText, "Downloads to: " + shortPath(Mods::folder(), 30));
  repositorySelection(layout);
}

void repositorySelection(UiLayout *layout) {
  if (UiImage *art = el<UiImage>(layout, kArt))
    art->setHidden(true);
  ModRepo::State s = ModRepo::state();
  std::string status = ModRepo::status();
  if (s == ModRepo::State::Installing)
    status = "Downloading... " + std::to_string(static_cast<int>(ModRepo::progress() * 100)) + "%";
  UiListBox *list = el<UiListBox>(layout, kList);
  int i = list ? list->getSelectedIndex() : -1;
  if (i < 0 || i >= static_cast<int>(g_repoRows.size())) {
    setText(layout, kAbout, status.empty() ? "The repository has nothing yet." : status);
    setLabel(layout, kToggle, "Download");
    setDetails(layout, "Its parts", {});
    return;
  }
  const ModRepo::Mod &m = g_repoMods[g_repoRows[i].mod];
  const ModRepo::Part &p = m.parts[g_repoRows[i].part];
  std::string have = ModRepo::installedVersion(m, p);
  std::string about = m.name + (m.version.empty() ? "" : "  (version " + m.version + ")");
  if (!m.author.empty())
    about += "\nby " + m.author;
  about += "\n\n" + p.name + ": " + sizeText(p.size) + (p.files ? ", " + std::to_string(p.files) + " files" : "");
  about += have.empty() ? "\nNot in your mods yet." : have == m.version ? "\nIn your mods." : "\nIn your mods: an older version (" + have + ").";
  if (!status.empty())
    about += "\n\n" + status;
  if (!m.about.empty())
    about += "\n\n" + m.about;
  if (!m.notice.empty())
    about += "\n\n" + m.notice;
  setText(layout, kAbout, about);
  setLabel(layout, kToggle, have.empty() ? "Download" : have == m.version ? "Download again" : "Update");
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
  return now != g_loaded;
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
  setLabel(layout, kRestart, "Restart");
  g_repo = false;
  setLabel(layout, kRepository, "Mod repository");
  // (only the official installs carry the repository's client, modrepo.dll:
  // the Game Build's Mod Menu is the player's own mods alone)
  if (UiButton *b = el<UiButton>(layout, kRepository))
    b->setHidden(!ModRepo::available());
  setLabel(layout, kAdd, "Add a mod...");
  if (UiListBox *list = el<UiListBox>(layout, kList))
    list->setSelectionAction(UiAction::MOD_LIST_SELECTION);
  refresh(layout, rm);
  return layout;
}

void refresh(UiLayout *layout, ResourceManager *rm) {
  if (g_repo)
    return repositoryRefresh(layout);
  UiListBox *list = el<UiListBox>(layout, kList);
  int keep = list ? list->getSelectedIndex() : -1;
  g_mods = Mods::list();
  if (list) {
    list->clear();
    for (const Mods::Mod &m : g_mods)
      list->addItem((m.enabled ? "[On]  " : "[Off]  ") + m.name, m.folder);
    if (!g_mods.empty())
      list->setSelectedIndex(keep >= 0 && keep < static_cast<int>(g_mods.size()) ? keep : 0);
  }
  setText(layout, kFolderText, "Folder: " + shortPath(Mods::folder(), 34));
  if (g_mods.empty()) {
    setText(layout, kAbout,
            "No mods in this folder yet.\n\nA mod is a folder with a mod.json and its files (a .ztd), "
            "made in the ZT1 Lab or by hand. Put one in the mods folder, or choose another folder.");
    setLabel(layout, kToggle, "Switch on");
    setDetails(layout, "It changes", {});
  }
  selectionChanged(layout, rm);
}

void selectionChanged(UiLayout *layout, ResourceManager *rm) {
  if (g_repo)
    return repositorySelection(layout);
  UiListBox *list = el<UiListBox>(layout, kList);
  int i = list ? list->getSelectedIndex() : -1;
  if (i < 0 || i >= static_cast<int>(g_mods.size()))
    return;
  const Mods::Mod &m = g_mods[i];
  std::string about = m.name;
  if (!m.version.empty())
    about += "  (version " + m.version + ")";
  if (!m.author.empty())
    about += "\nby " + m.author;
  about += std::string("\n\n") + (m.enabled ? "Switched on" : "Switched off");
  if (m.enabled != (g_loaded.count(m.folder) > 0))
    about += m.enabled ? " - loads when the game restarts (Restart)" : " - goes when the game restarts (Restart)";
  if (!m.about.empty())
    about += "\n\n" + m.about;
  setText(layout, kAbout, about);
  setLabel(layout, kToggle, m.enabled ? "Switch off" : "Switch on");
  // What it changes, in the objectives' list
  std::vector<std::string> lines = m.replaces;
  if (lines.empty())
    lines.push_back(m.settings.empty() ? "(it doesn't say)" : "Its own settings: Game Options > Mods & Updates");
  setDetails(layout, "It changes", lines);
  // Its art: the first frame it changes (as loaded now: the mod's own when
  // it's on), with the palette it names
  if (UiImage *art = el<UiImage>(layout, kArt)) {
    art->setHidden(true);
    if (!m.preview.empty() && !m.palette.empty() && rm->hasResource(m.preview)) {
      art->setZt1Image(m.preview, g_loaded.count(m.folder) ? m.palette : m.originalPalette);
      art->setHidden(false);
    }
  }
}

void toggle(UiLayout *layout, ResourceManager *rm) {
  if (g_repo) {
    // (the repository's view: the picked part downloaded)
    UiListBox *list = el<UiListBox>(layout, kList);
    int i = list ? list->getSelectedIndex() : -1;
    if (i >= 0 && i < static_cast<int>(g_repoRows.size())) {
      const ModRepo::Mod &m = g_repoMods[g_repoRows[i].mod];
      ModRepo::install(m.id, m.parts[g_repoRows[i].part].id);
      tick(layout, rm);
    }
    return;
  }
  UiListBox *list = el<UiListBox>(layout, kList);
  int i = list ? list->getSelectedIndex() : -1;
  if (i < 0 || i >= static_cast<int>(g_mods.size()))
    return;
  Mods::setEnabled(g_mods[i].folder, !g_mods[i].enabled);
  refresh(layout, rm);
}

void chooseFolder(UiLayout *layout, ResourceManager *rm) {
#ifdef _WIN32
  BROWSEINFOA bi{};
  char name[MAX_PATH] = {};
  bi.pszDisplayName = name;
  bi.lpszTitle = "The folder your mods are in";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  if (LPITEMIDLIST id = SHBrowseForFolderA(&bi)) {
    char path[MAX_PATH] = {};
    if (SHGetPathFromIDListA(id, path))
      Mods::setFolder(path);
    CoTaskMemFree(id);
  }
#endif
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
          list->setSelectedIndex(i);
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
  if (UiButton *b = el<UiButton>(layout, kFolderButton))
    b->setHidden(g_repo);
  if (g_repo) {
    ModRepo::fetch();
    g_repoShown = ModRepo::state();
    g_repoPercent = -1;
  }
  refresh(layout, rm);
}

void tick(UiLayout *layout, ResourceManager *rm) {
  if (!g_repo)
    return;
  // (the list again once the catalogue's in or a download's done; the
  // details as a download goes)
  ModRepo::State s = ModRepo::state();
  int percent = s == ModRepo::State::Installing ? static_cast<int>(ModRepo::progress() * 100) : -1;
  if (s != g_repoShown) {
    g_repoShown = s;
    g_repoPercent = percent;
    refresh(layout, rm);
  } else if (percent != g_repoPercent) {
    g_repoPercent = percent;
    repositorySelection(layout);
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
