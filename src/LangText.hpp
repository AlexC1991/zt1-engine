#ifndef LANG_TEXT_HPP
#define LANG_TEXT_HPP

#include <string>

// A named TEXT resource of the language DLLs (lang0.dll: AWARD_AWARD1,
// FREEFORM_DONATE1, SCN11_NOBUY_ANIMALS - the scenario popups' words), the
// later DLLs first; empty when there's none
std::string langText(const std::string &name);
// ours: Windows-1252 text (the game's) as UTF-8
std::string windows1252ToUtf8(const std::string &in);

#endif // LANG_TEXT_HPP
