// RBS Engine - virtual file system.
//
// Game data is looked up in layers: the active mod folder(s) first, then base/.
// A mod can therefore override any file (texture, model, script, map...) simply by
// shipping a file with the same relative path, or add brand new files.
#pragma once

#include <string>
#include <vector>

namespace rbs {

namespace vfs {

// Locates the data root (folder containing base/). Returns false if not found.
bool Init(const char* argv0);
// Activates a mod (folder name inside mods/). Empty string = base game only.
void SetMod(const std::string& mod);
const std::string& CurrentMod();
const std::string& Root();          // absolute folder containing base/ and mods/
std::string WriteDir();             // where new/edited files are saved (mod folder or base)
std::vector<std::string> ListMods();

// Resolves a relative game path ("textures/brick.png") to a real file path, or "".
std::string Resolve(const std::string& relPath);
// Like Resolve, but tries every extension in each layer before moving to the next one,
// so a mod's "textures/brick.tga" overrides base's "textures/brick.png".
std::string ResolveAny(const std::string& relStem, const std::vector<std::string>& exts);
bool Exists(const std::string& relPath);
bool ReadText(const std::string& relPath, std::string& out);
bool WriteText(const std::string& relPath, const std::string& text);  // writes into WriteDir()
// Lists files in a game folder from all layers (relative paths, unique, sorted).
std::vector<std::string> List(const std::string& relDir, const std::string& ext, bool recursive = false);
// Extra absolute folders searched for textures (editor "Load Texture Folder").
void AddTextureFolder(const std::string& absDir);
const std::vector<std::string>& TextureFolders();

}  // namespace vfs
}  // namespace rbs
