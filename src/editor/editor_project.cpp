// RBS Engine - editor: game projects (new / open / settings / build) and enemy, item and
// weapon types made without code (entities/*.json, weapons/*.json edited in the Inspector).
#include "app/app.h"
#include "app/project_tools.h"
#include "core/log.h"
#include "core/project.h"
#include "core/vfs.h"
#include "editor/editor_impl.h"
#include "ui/imgui.h"

#include <filesystem>
#include <sstream>

namespace rbs {

using namespace ui;
namespace fs = std::filesystem;

static const float kRow = 22;

// draws text wrapped to `width`, returns the y below it
static float Wrapped(const std::string& text, float x, float y, float width, Color c, int size = 13) {
    std::istringstream words(text);
    std::string line;
    for (std::string word; words >> word;) {
        std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && TextWidth(next, size) > width) {
            Text(line, x, y, c, size);
            y += size + 5;
            next = word;
        }
        line = next;
    }
    if (!line.empty()) {
        Text(line, x, y, c, size);
        y += size + 5;
    }
    return y;
}

// values of hand-edited JSON files can have any type
static std::string JStr(const json& j, const char* key, const std::string& def = "") {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : def;
}

static bool JBool(const json& j, const char* key, bool def) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : def;
}

static bool ValidDefName(const std::string& n) {
    if (n.empty() || n.size() > 48) return false;
    for (char c : n)
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '_')) return false;
    return true;
}

// ---------------------------------------------------------------------------------
// types and weapons made without code
// ---------------------------------------------------------------------------------
std::vector<std::string> Editor::Impl::VariantBases() const {
    std::vector<std::string> out;
    for (auto& c : classes) {
        if (c.brush) continue;
        if (c.category == "monsters" || c.category == "items" || c.category == "props" || c.category == "effects" || c.category == "sound" ||
            !c.definition.empty())
            out.push_back(c.name);
    }
    return out;
}

void Editor::Impl::SelectAsset(const std::string& kind, const std::string& name) {
    if (inspectKind == kind && inspectName == name) return;
    inspectKind = kind;
    inspectName = name;
    assetDirty = false;
    assetConfirmDelete = false;
    assetPath.clear();
    assetDef = json::object();
    if (kind == "class") {
        if (const ClassInfo* ci = ClassFor(name)) assetPath = ci->definition;
    } else if (kind == "weapon") {
        int idx = script.WeaponIndex(name);
        if (idx >= 0) assetPath = script.weapons[idx].definition;
    }
    std::string text;
    if (!assetPath.empty() && vfs::ReadText(assetPath, text)) {
        json j = json::parse(text, nullptr, false, true);
        if (j.is_object()) assetDef = j;
    }
    if (kind == "class" && !(assetDef.contains("properties") && assetDef["properties"].is_object())) assetDef["properties"] = json::object();
}

bool Editor::Impl::CreateDefinition(const std::string& kind, const std::string& name, const std::string& base) {
    if (!ValidDefName(name)) {
        SetStatus("Use lowercase letters, digits and _ for the name");
        return false;
    }
    if (kind == "class" ? ClassFor(name) != nullptr || script.HasClass(name) : script.WeaponIndex(name) >= 0) {
        SetStatus("'" + name + "' already exists");
        return false;
    }
    json def;
    std::string path;
    if (kind == "class") {
        const ClassInfo* bi = ClassFor(base);
        def = {{"base", base}, {"description", bi && !bi->desc.empty() ? bi->desc : name}, {"properties", json::object()}};
        path = "entities/" + name + ".json";
    } else {
        int idx = script.WeaponIndex(base);
        // a full copy of the base weapon's data, so every field is right there to change
        def = script.WeaponData(base);
        def["base"] = base;
        def["name"] = name;
        int maxSlot = 0;
        for (auto& w : script.weapons) maxSlot = std::max(maxSlot, w.slot);
        def["slot"] = idx >= 0 ? maxSlot + 1 : 1;
        path = "weapons/" + name + ".json";
    }
    if (!vfs::WriteText(path, def.dump(2) + "\n")) {
        SetStatus("Cannot write " + path);
        return false;
    }
    RefreshClasses();
    inspectKind.clear();
    SelectAsset(kind, name);
    std::string where = vfs::CurrentMod().empty() ? "base/" : "mods/" + vfs::CurrentMod() + "/";
    SetStatus("Created " + where + path);
    return true;
}

void Editor::Impl::SaveAsset() {
    if (assetPath.empty()) return;
    if (!vfs::WriteText(assetPath, assetDef.dump(2) + "\n")) {
        SetStatus("Cannot write " + assetPath);
        return;
    }
    std::string kind = inspectKind, name = inspectName;
    RefreshClasses();  // reload the definitions so placed entities use the new values
    inspectKind.clear();
    SelectAsset(kind, name);
    meshDirty = true;
    SetStatus("Saved " + assetPath);
}

void Editor::Impl::DeleteAsset() {
    if (assetPath.empty()) return;
    fs::path p = fs::path(vfs::WriteDir()) / assetPath;
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        SetStatus(assetPath + " belongs to another layer (base or a different project) - not deleted");
        return;
    }
    fs::remove(p, ec);
    SetStatus("Deleted " + assetPath);
    inspectKind.clear();
    inspectName.clear();
    RefreshClasses();
}

float Editor::Impl::InspectAsset(Rectangle r, float y) {
    bool isClass = inspectKind == "class";
    const ClassInfo* ci = isClass ? ClassFor(inspectName) : nullptr;
    int widx = isClass ? -1 : script.WeaponIndex(inspectName);
    if ((isClass && !ci) || (!isClass && widx < 0)) {
        inspectKind.clear();
        return y;
    }
    Text(std::string(isClass ? "Entity type: " : "Weapon: ") + inspectName, r.x + 12, y + 4, theme.text, 15, "ui_bold");
    y += kRow + 4;
    if (isClass && !ci->desc.empty()) y = Wrapped(ci->desc, r.x + 12, y, r.width - 24, theme.textDim) + 4;

    if (assetPath.empty()) {
        // defined in code (Lua / native plugin): offer a no-code variant
        y = Wrapped(isClass ? "Defined in code (a Lua script or native plugin). Make a variant to change its properties without "
                              "programming - health, speed, AI, attacks, model, sounds..."
                            : "Defined in code (a Lua script or native plugin). Make a new weapon from it to change damage, fire "
                              "rate, ammo, model... without programming.",
                    r.x + 12, y, r.width - 24, theme.textDim) +
            6;
        if (Button("variant", {r.x + 10, y, r.width - 20, kRow + 4}, isClass ? "Create a variant (no code)" : "Create a new weapon from this")) {
            newDefBase = inspectName;
            newDefName = inspectName + "_2";
            dialog = isClass ? "newtype" : "newweapon";
        }
        y += kRow + 10;
        std::string placeClass = isClass ? inspectName : "weapon_" + inspectName;
        if (ClassFor(placeClass)) {
            if (Button("place", {r.x + 10, y, r.width - 20, kRow}, isClass ? "Place in the map (Entity tool)" : "Place its pickup in the map")) {
                curClass = placeClass;
                tool = Tool::Entity;
            }
            y += kRow + 8;
        }
        Text(isClass ? "Default properties" : "Fields", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
        y += kRow - 2;
        json values = isClass ? ci->props : script.WeaponData(inspectName);
        for (auto it = values.begin(); it != values.end(); ++it) {
            std::string v = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
            Text(it.key(), r.x + 14, y, theme.textDim, 12);
            Text(Ellipsize(v, r.width - 140, 12), r.x + 124, y, theme.text, 12);
            y += 17;
        }
        return y + 10;
    }

    // a definition file: everything is editable
    Text(Ellipsize(assetPath, r.width - 24, 12), r.x + 12, y, theme.textDim, 12);
    y += 18;
    std::string base = JStr(assetDef, "base");
    if (!base.empty()) {
        Text("Based on: " + base, r.x + 12, y, theme.textDim, 12);
        y += 18;
    }
    if (isClass) {
        std::string desc = JStr(assetDef, "description");
        Text("Description", r.x + 12, y + 4, theme.text, 13);
        if (TextField("adesc", {r.x + 114, y, r.width - 124, kRow - 2}, desc)) {
            assetDef["description"] = desc;
            assetDirty = true;
        }
        y += kRow + 4;
        Text("Properties", r.x + 12, y + 2, theme.textDim, 12, "ui_bold");
        y += kRow - 2;
        const ClassInfo* bi = ClassFor(base);
        json defaults = bi ? bi->props : json::object();
        y = PropEditor("aprops", r, y, assetDef["properties"], defaults, true, bi ? &bi->enums : nullptr, bi ? &bi->help : nullptr, &assetDirty);
    } else {
        json defaults = base.empty() ? json::object() : script.WeaponData(base);
        json fields = assetDef;
        fields.erase("base");
        const json* enums = weaponFields.contains("enums") ? &weaponFields["enums"] : nullptr;
        const json* help = weaponFields.contains("help") ? &weaponFields["help"] : nullptr;
        bool changed = false;
        y = PropEditor("afields", r, y, fields, defaults, true, enums, help, &changed);
        if (changed) {
            if (!base.empty()) fields["base"] = base;
            assetDef = fields;
            assetDirty = true;
        }
    }
    y += 8;
    float bw = (r.width - 26) / 2;
    if (Button("asave", {r.x + 10, y, bw, kRow + 2}, assetDirty ? "Save *" : "Save", assetDirty)) SaveAsset();
    if (Button("arevert", {r.x + 16 + bw, y, bw, kRow + 2}, "Revert", false, assetDirty)) {
        std::string kind = inspectKind, name = inspectName;
        inspectKind.clear();
        SelectAsset(kind, name);
    }
    y += kRow + 8;
    std::string placeClass = isClass ? inspectName : "weapon_" + inspectName;
    if (ClassFor(placeClass) && Button("aplace", {r.x + 10, y, bw, kRow}, isClass ? "Place (Entity tool)" : "Place pickup")) {
        curClass = placeClass;
        tool = Tool::Entity;
    }
    if (Button("adel", {r.x + 16 + bw, y, bw, kRow}, assetConfirmDelete ? "Really delete?" : "Delete")) {
        if (assetConfirmDelete) DeleteAsset();
        else assetConfirmDelete = true;
        return y + kRow;
    }
    y += kRow + 8;
    y = Wrapped("Changes apply to every placed one that does not override the value itself. Press Play (F5) to try it.", r.x + 12, y,
                r.width - 24, theme.textDim, 12);
    return y + 10;
}

// ---------------------------------------------------------------------------------
// project dialog
// ---------------------------------------------------------------------------------
void Editor::Impl::OpenProjectDialog() {
    projEdit = project::Current().manifest;
    if (!projEdit.is_object()) projEdit = json::object();
    projMessage.clear();
    projOpen.clear();
    dialog = "project";
}

void Editor::Impl::DrawProjectDialog(int w, int h) {
    Fill({0, 0, (float)w, (float)h}, Color{0, 0, 0, 110});
    auto close = [&]() {
        dialog.clear();
        ClearFocus();
    };
    const project::Info& info = project::Current();

    if (dialog == "newtype" || dialog == "newweapon") {
        bool weapon = dialog == "newweapon";
        float dw = 480, dh = 230;
        Rectangle d{w * 0.5f - dw * 0.5f, h * 0.5f - dh * 0.5f, dw, dh};
        Fill(d, theme.panelAlt);
        Outline(d, theme.border);
        Text(weapon ? "New weapon" : "New entity type", d.x + 14, d.y + 12, theme.text, 16, "ui_bold");
        Text("Name", d.x + 14, d.y + 50, theme.text, 13);
        TextField("defname", {d.x + 120, d.y + 46, dw - 134, 24}, newDefName, "e.g. zombie");
        Text("Based on", d.x + 14, d.y + 84, theme.text, 13);
        std::vector<std::string> bases = weapon ? weaponIds : VariantBases();
        if (newDefBase.empty() && !bases.empty()) newDefBase = bases[0];
        Dropdown("defbase", {d.x + 120, d.y + 80, dw - 134, 24}, newDefBase, bases);
        Wrapped(weapon ? "Starts as a copy of that weapon - then change damage, fire rate, ammo, model... in the Inspector. Saved as weapons/<name>.json in the project."
                       : "Starts with the behaviour of that type - then change health, speed, AI, attacks, model... in the Inspector. Saved as entities/<name>.json in the project.",
                d.x + 14, d.y + 116, dw - 28, theme.textDim, 12);
        if (Button("defok", {d.x + dw - 200, d.y + dh - 40, 90, 28}, "Create") || (IsKeyPressed(KEY_ENTER) && !newDefName.empty())) {
            if (CreateDefinition(weapon ? "weapon" : "class", newDefName, newDefBase)) close();
        }
        if (Button("defcancel", {d.x + dw - 104, d.y + dh - 40, 90, 28}, "Cancel") || IsKeyPressed(KEY_ESCAPE)) close();
        return;
    }

    if (dialog == "newproject") {
        float dw = 480, dh = 250;
        Rectangle d{w * 0.5f - dw * 0.5f, h * 0.5f - dh * 0.5f, dw, dh};
        Fill(d, theme.panelAlt);
        Outline(d, theme.border);
        Text("New game project", d.x + 14, d.y + 12, theme.text, 16, "ui_bold");
        Text("Game title", d.x + 14, d.y + 50, theme.text, 13);
        TextField("ptitle", {d.x + 130, d.y + 46, dw - 144, 24}, projTitle, "My Shooter");
        Text("Folder", d.x + 14, d.y + 84, theme.text, 13);
        if (projName.empty() && !projTitle.empty()) {
            // suggest a folder name from the title
            for (char c : projTitle)
                if (isalnum((unsigned char)c)) projName += (char)tolower((unsigned char)c);
                else if ((c == ' ' || c == '-' || c == '_') && !projName.empty() && projName.back() != '_') projName += '_';
        }
        TextField("pname", {d.x + 130, d.y + 80, dw - 144, 24}, projName, "my_shooter");
        Wrapped("Creates mods/<folder>/ with a starter level and opens it. Your game can use everything from base/ (textures, "
                "models, enemies, weapons) and add its own.",
                d.x + 14, d.y + 114, dw - 28, theme.textDim, 12);
        if (!projMessage.empty()) Text(projMessage, d.x + 14, d.y + dh - 64, theme.warn, 12);
        if (Button("pcreate", {d.x + dw - 200, d.y + dh - 40, 90, 28}, "Create")) {
            std::string err;
            if (project::Create(projName, projTitle, &err)) {
                pendingProject = projName;
                pendingProjectMap = "maps/start.json";
                close();
            } else {
                projMessage = err;
            }
        }
        if (Button("pcancel", {d.x + dw - 104, d.y + dh - 40, 90, 28}, "Cancel") || IsKeyPressed(KEY_ESCAPE)) dialog = "project";
        return;
    }

    // "project": the hub
    float dw = 600, dh = info.folder.empty() ? 270 : 716;
    Rectangle d{w * 0.5f - dw * 0.5f, std::max(8.0f, h * 0.5f - dh * 0.5f), dw, dh};
    Fill(d, theme.panelAlt);
    Outline(d, theme.border);
    Text("Game project", d.x + 14, d.y + 12, theme.text, 16, "ui_bold");
    float y = d.y + 44, lx = d.x + 14, fx = d.x + 190, fw = dw - 204;
    if (info.folder.empty()) {
        y = Wrapped("No project is open - you are editing the base game (the demo). Create a project for your own game: it gets "
                    "its own maps, enemy types, weapons, menu and can be exported as a standalone game.",
                    lx, y, dw - 28, theme.textDim) +
            10;
    } else {
        Text(info.title + "   (mods/" + info.folder + ")", lx, y, theme.accent, 14, "ui_bold");
        y += 30;
        auto field = [&](const char* label, const char* key, const char* hint, json& obj) {
            Text(label, lx, y + 4, theme.text, 13);
            std::string v = JStr(obj, key);
            if (TextField(std::string("pf_") + key, {fx, y, fw, kRow}, v, hint)) obj[key] = v;
            y += kRow + 6;
        };
        field("Title", "title", "My Shooter", projEdit);
        field("Author", "author", "your name", projEdit);
        field("Version", "version", "1.0", projEdit);
        field("Description", "description", "one line about the game", projEdit);
        // start map: maps of the project first
        Text("Start map", lx, y + 4, theme.text, 13);
        std::string start = JStr(projEdit, "start_map", "maps/start.json");
        if (Dropdown("pf_start", {fx, y, fw, kRow}, start, mapList)) projEdit["start_map"] = start;
        y += kRow + 12;
        Text("Main menu", lx, y, theme.textDim, 12, "ui_bold");
        y += 20;
        if (!projEdit.contains("menu") || !projEdit["menu"].is_object()) projEdit["menu"] = json::object();
        json& menu = projEdit["menu"];
        field("Menu title", "title", "big title in the menu", menu);
        field("Subtitle", "subtitle", "line under the title", menu);
        Text("Background", lx, y + 4, theme.text, 13);
        std::string bg = JStr(menu, "background");
        Rectangle bgr{fx, y, fw, kRow};
        if (TextField("pf_bg", bgr, bg, "drop a sprite/image here (sprites/menu.png)")) menu["background"] = bg;
        if (Hovered(bgr) && DragReleased() && DragType() == "sprite") menu["background"] = DragPayload();
        y += kRow + 6;
        bool showEd = JBool(menu, "show_editor", true);
        if (Checkbox("pf_ed", {lx, y, dw - 28, kRow}, showEd, "Offer the level editor in the exported game's menu")) menu["show_editor"] = showEd;
        y += kRow + 4;
        bool showMods = JBool(menu, "show_mods", true);
        if (Checkbox("pf_mods", {lx, y, dw - 28, kRow}, showMods, "Offer the Mods page (other games and mods in its mods/ folder)")) menu["show_mods"] = showMods;
        y += kRow + 10;
        Text("Player at the start of a level", lx, y, theme.textDim, 12, "ui_bold");
        y += 20;
        if (!projEdit.contains("player") || !projEdit["player"].is_object()) projEdit["player"] = json::object();
        json& pl = projEdit["player"];
        auto num = [&](const char* label, const char* key, float def) {
            Text(label, lx, y + 4, theme.text, 13);
            float v = pl.contains(key) && pl[key].is_number() ? pl[key].get<float>() : def;
            if (FloatField(std::string("pn_") + key, {fx, y, 120, kRow}, v, 1.0f, "%g")) pl[key] = (int)roundf(v);
            y += kRow + 6;
        };
        num("Health", "health", 100);
        num("Armor", "armor", 0);
        Text("Start weapons", lx, y + 4, theme.text, 13);
        std::string sw;
        if (pl.contains("start_weapons") && pl["start_weapons"].is_array())
            for (auto& v : pl["start_weapons"])
                if (v.is_string()) sw += (sw.empty() ? "" : " ") + v.get<std::string>();
        if (TextField("pn_sw", {fx, y, fw, kRow}, sw, "pistol shotgun")) {
            json arr = json::array();
            std::istringstream ws(sw);
            for (std::string id; ws >> id;) arr.push_back(id);
            pl["start_weapons"] = arr;
        }
        if (Hovered({fx, y, fw, kRow})) {
            std::string ids;
            for (auto& id : weaponIds) ids += (ids.empty() ? "" : ", ") + id;
            Tooltip("weapon ids separated by spaces: " + ids);
        }
        y += kRow + 6;
        Text("Selected weapon", lx, y + 4, theme.text, 13);
        std::string sel = JStr(pl, "select");
        if (Dropdown("pn_sel", {fx, y, fw, kRow}, sel, weaponIds)) pl["select"] = sel;
        y += kRow + 10;
        auto saveSettings = [&]() {
            std::string err;
            if (!project::SaveManifest(projEdit, &err)) {
                projMessage = err;
                return false;
            }
            app->RefreshProjectSettings();
            projMessage = "Settings saved.";
            return true;
        };
        if (Button("psave", {lx, y, 160, 26}, "Save settings")) saveSettings();
        y += 40;
        Text("Export", lx, y, theme.textDim, 12, "ui_bold");
        y += 20;
        Text("Builds a standalone game into builds/" + info.folder + "/ - the engine, base/, your project and licenses.", lx, y, theme.textDim, 12);
        y += 22;
        Checkbox("pf_demo", {lx, y, 300, kRow}, projDemoMaps, "Include the demo levels (E1M1)");
        if (Button("pbuild", {d.x + dw - 174, y - 2, 160, 28}, "Build game")) {
            // builds what the dialog shows: unsaved settings and the open map are saved first
            if (dirty && !path.empty()) SaveMap(path);
            if (projEdit == info.manifest || saveSettings()) {
                project::BuildOptions o;
                o.includeEditor = project::Current().showEditor;
                o.includeDemoMaps = projDemoMaps;
                project::Build(o, &projMessage);
            }
        }
        y += kRow + 10;
    }
    if (!projMessage.empty()) y = Wrapped(projMessage, lx, y, dw - 28, theme.accent, 12) + 4;

    // bottom row: new / open / close
    float by = d.y + dh - 42;
    if (Button("pnew", {lx, by, 140, 28}, "New project...")) {
        projName.clear();
        projTitle.clear();
        projMessage.clear();
        dialog = "newproject";
        return;
    }
    std::vector<std::string> projects = {"base"};
    for (auto& m : vfs::ListMods()) projects.push_back(m);
    if (projOpen.empty()) projOpen = info.folder.empty() ? "base" : info.folder;
    Dropdown("popen", {lx + 150, by, 190, 28}, projOpen, projects);
    bool other = projOpen != (info.folder.empty() ? "base" : info.folder);
    if (Button("popenbtn", {lx + 346, by, 80, 28}, "Open", false, other)) {
        if (dirty && !path.empty()) SaveMap(path);
        pendingProject = projOpen;
        pendingProjectMap = projOpen == "base" ? "" : project::Read(projOpen).startMap;
        close();
        return;
    }
    if (Button("pclose", {d.x + dw - 104, by, 90, 28}, "Close") || IsKeyPressed(KEY_ESCAPE)) close();
    if (dirty && other)
        Text(path.empty() ? "The open map was never saved - its changes will be lost." : "The open map has unsaved changes - they are saved first.", lx,
             by - 18, theme.warn, 12);
}

}  // namespace rbs
