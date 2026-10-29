// RBS Engine - native plugins (shared libraries using sdk/include/rbs_plugin.h).
#pragma once

#include "script/script.h"

namespace rbs {

class Game;
struct Entity;

namespace plugins {

void SetGame(Game* g);
void LoadAll();    // loads plugins/*.dll|so|dylib from base and the active mod
void UnloadAll();  // calls rbs_plugin_shutdown and unloads the libraries
std::vector<std::string> Loaded();

int FindClass(const std::string& name);      // behaviour index or -1
int FindComponent(const std::string& name);  // behaviour index or -1
const std::vector<ClassInfo>& ClassInfos();
const std::vector<ClassInfo>& ComponentInfos();

void* Create(int b, Entity* e, const json& props);
void Destroy(int b, void* data, Entity* e);
void Spawn(int b, void* data, Entity* e);
void Update(int b, void* data, Entity* e, float dt);
void Touch(int b, void* data, Entity* e, Entity* other, Vector3 n);
void Use(int b, void* data, Entity* e, Entity* activator);
float Damaged(int b, void* data, Entity* e, float amount, Entity* attacker, Vector3 dir);
void Pain(int b, void* data, Entity* e, float amount, Entity* attacker);
void Die(int b, void* data, Entity* e, Entity* attacker, float amount);
void Blocked(int b, void* data, Entity* e, Entity* other);

}  // namespace plugins
}  // namespace rbs
