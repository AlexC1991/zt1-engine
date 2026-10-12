#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <string>

// The player's settings in zoo.ini (Paths::configFile()), read and written
// as the original keeps them: the Game Options' [UI] keys (userAttenuation,
// menuMusicAttenuation, helpType, tooltipDelay, tooltipDuration,
// mouseScrollX/Y), [user] (the window), and - ours - [Extras], the engine's
// additions switched on or off (Features.hpp). Writing one key keeps the
// rest of the file as it is.
namespace Settings {

std::string get(const std::string &section, const std::string &key, const std::string &fallback = "");
int getInt(const std::string &section, const std::string &key, int fallback);
void set(const std::string &section, const std::string &key, const std::string &value);
void setInt(const std::string &section, const std::string &key, int value);

// The extras' switches from [Extras] (each left as it is when not there)
void loadExtras();
void saveExtra(const std::string &name, bool on);

// Puts the Game Options' settings to work: the sound's and the menu music's
// volumes, the map's scrolling speed (at start, and after each change)
void apply();

} // namespace Settings

#endif // SETTINGS_HPP
