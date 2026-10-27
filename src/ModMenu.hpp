#ifndef MODMENU_HPP
#define MODMENU_HPP

#include <string>

class ResourceManager;
class UiLayout;

// The Mod Menu (with the "modmenu" switch on, the main menu's "Get New Zoo
// Tycoon Items" opens it; off, that button does what ours always has -
// nothing: the original's download service is long gone). A window of the
// original's own art - the Tutorial & Scenario screen's background, list,
// story and objectives, scroll bars and button frames - listing the mods in
// the mods folder (Mods.hpp): each one's name, author and art, its details
// where a scenario's story is and what it changes where its objectives are;
// switch optional mods on or off, with live refresh. Required content stays
// enabled while the active map or zoo uses it.
namespace ModMenu {
// The window, built on the game's own map-select layout (its path given)
UiLayout *open(ResourceManager *rm, const std::string &layoutPath);
// The list and the picked mod's details shown again
void refresh(UiLayout *layout, ResourceManager *rm);
void selectionChanged(UiLayout *layout, ResourceManager *rm);
// The picked mod switched on or off
void toggle(UiLayout *layout, ResourceManager *rm);
// Another mods folder (the Windows folder picker)
void chooseFolder(UiLayout *layout, ResourceManager *rm);
// Restart the game to load the mods switched on: false when it can't (in
// the Lab's Play view: restart Play there) - the message says so
bool restart(UiLayout *layout);
// What's changed since the game started (a restart loads it)
bool changed();
// The list switched between your mods and the mod repository's
// (ModRepo.hpp: the official install's; its Switch button downloads)
void repository(UiLayout *layout, ResourceManager *rm);
// Add a mod... (ModImport.hpp): an old .ztd, a .zip or a mod's folder put
// in the mods folder, for this game only
void addMod(UiLayout *layout, ResourceManager *rm);
// Each frame while it's open: the repository's list and a download's
// progress kept up to date
void tick(UiLayout *layout, ResourceManager *rm);
} // namespace ModMenu

#endif // MODMENU_HPP
