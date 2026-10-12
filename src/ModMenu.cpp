#include "ModMenu.hpp"

#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "Embed.hpp"
#include "IniReader.hpp"
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
#include <shlobj.h>
#endif

namespace {
// The map-select screen's ids (ui/mapselec.lyt), used as the Mod Menu's
constexpr int kList = 11504, kAbout = 11507, kArt = 11501;
constexpr int kArtLabel = 11502, kListLabel = 11503, kAboutLabel = 11506;
constexpr int kToggle = 11513; // its Play button: Switch on / off
// (the starting cash and difficulty: not for mods)
constexpr int kHide[] = {11509, 11510, 11511, 11512, 11530, 11531};
// Ours, in the same style: the folder, its button, Restart, a status line
constexpr int kFolderText = 7142, kFolderButton = 7141, kRestart = 7143;

// What the window adds to the map-select layout: the original's own fonts,
// colours and button frame (ui/gameopts/textbck)
const char *kExtra = R"(
[Mods folder]
type=UIText
id=7142
x=20
y=524
dx=336
dy=30
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
x=40
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
)";

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
  text += kExtra;
  IniReader *ini = new IniReader(text.data(), text.size());
  UiLayout *layout = new UiLayout(ini, rm);
  for (int id : kHide)
    if (UiElement *e = layout->getElementById(id))
      e->setHidden(true);
  setText(layout, kListLabel, "Mods");
  setText(layout, kArtLabel, "Its art");
  setText(layout, kAboutLabel, "About it");
  setLabel(layout, kFolderButton, "Folder...");
  setLabel(layout, kRestart, "Restart");
  if (UiListBox *list = el<UiListBox>(layout, kList))
    list->setSelectionAction(UiAction::MOD_LIST_SELECTION);
  refresh(layout, rm);
  return layout;
}

void refresh(UiLayout *layout, ResourceManager *rm) {
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
  }
  selectionChanged(layout, rm);
}

void selectionChanged(UiLayout *layout, ResourceManager *rm) {
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
  if (!m.replaces.empty()) {
    about += "\n\nIt changes:";
    for (const std::string &f : m.replaces)
      about += "\n  " + f;
  }
  setText(layout, kAbout, about);
  setLabel(layout, kToggle, m.enabled ? "Switch off" : "Switch on");
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
