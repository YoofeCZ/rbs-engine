// RBS Engine - tiny localisation: lang/<code>.json maps keys to translated strings.
#pragma once

#include <string>

namespace rbs {
const char* Tr(const char* key);  // returns the translation or the key itself
void ReloadLanguage();
}  // namespace rbs
