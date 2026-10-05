#ifndef LANG_TEXT_HPP
#define LANG_TEXT_HPP

#include <string>

// A named TEXT resource of the language DLLs (lang0.dll: AWARD_AWARD1,
// FREEFORM_DONATE1, SCN11_NOBUY_ANIMALS - the scenario popups' words), the
// later DLLs first; empty when there's none
std::string langText(const std::string &name);

#endif // LANG_TEXT_HPP
