#include "world/map.h"
#include "core/log.h"
#include "core/vfs.h"

namespace rbs {

json Vec3ToJson(Vector3 v) {
    auto r = [](float f) { return roundf(f * 10000.0f) / 10000.0f; };
    return json::array({r(v.x), r(v.y), r(v.z)});
}

Vector3 JsonToVec3(const json& j, Vector3 def) {
    if (j.is_array() && j.size() >= 3 && j[0].is_number()) return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    if (j.is_string()) {
        Vector3 v = def;
        if (sscanf(j.get<std::string>().c_str(), "%f %f %f", &v.x, &v.y, &v.z) == 3) return v;
    }
    return def;
}

std::string MapEntity::Str(const char* key, const std::string& def) const {
    auto it = props.find(key);
    if (it == props.end()) return def;
    if (it->is_string()) return it->get<std::string>();
    return it->dump();
}

float MapEntity::Num(const char* key, float def) const {
    auto it = props.find(key);
    if (it == props.end()) return def;
    if (it->is_number()) return it->get<float>();
    if (it->is_boolean()) return it->get<bool>() ? 1.0f : 0.0f;
    if (it->is_string()) {
        const std::string& s = it->get_ref<const std::string&>();
        char* end = nullptr;
        float f = strtof(s.c_str(), &end);
        return end != s.c_str() ? f : def;
    }
    return def;
}

Vector3 MapEntity::Vec(const char* key, Vector3 def) const {
    auto it = props.find(key);
    if (it == props.end()) return def;
    return JsonToVec3(*it, def);
}

void Map::Clear() {
    settings = MapSettings();
    brushes.clear();
    entities.clear();
}

static json FaceToJson(const BrushFace& f) {
    json j;
    j["n"] = Vec3ToJson(f.plane.n);
    j["d"] = roundf(f.plane.d * 10000.0f) / 10000.0f;
    j["tex"] = f.tex.name;
    if (f.tex.offU != 0 || f.tex.offV != 0) j["off"] = {f.tex.offU, f.tex.offV};
    if (f.tex.scaleU != 1 || f.tex.scaleV != 1) j["scale"] = {f.tex.scaleU, f.tex.scaleV};
    if (f.tex.rot != 0) j["rot"] = f.tex.rot;
    return j;
}

static json BrushToJson(const Brush& b) {
    json faces = json::array();
    for (auto& f : b.faces) faces.push_back(FaceToJson(f));
    return json{{"faces", faces}};
}

static bool BrushFromJson(const json& jb, Brush& b) {
    b.faces.clear();
    // Shorthand: {"box":[[minx,miny,minz],[maxx,maxy,maxz]], "tex":"brick"}
    if (jb.contains("box")) {
        Vector3 mn = JsonToVec3(jb["box"][0]), mx = JsonToVec3(jb["box"][1]);
        b = MakeBoxBrush(VMin(mn, mx), VMax(mn, mx), jb.value("tex", std::string("concrete")));
        if (jb.contains("faces_tex")) {
            // per side override: {"+x":"brick", "-y":"floor"...}
            const char* keys[6] = {"+x", "-x", "+y", "-y", "+z", "-z"};
            for (int i = 0; i < 6 && i < (int)b.faces.size(); ++i)
                if (jb["faces_tex"].contains(keys[i])) b.faces[i].tex.name = jb["faces_tex"][keys[i]].get<std::string>();
        }
        return b.BuildPolygons();
    }
    for (auto& jf : jb["faces"]) {
        BrushFace f;
        f.plane.n = Norm(JsonToVec3(jf["n"], {0, 1, 0}));
        f.plane.d = jf.value("d", 0.0f);
        f.tex.name = jf.value("tex", std::string("concrete"));
        if (jf.contains("off")) f.tex.offU = jf["off"][0], f.tex.offV = jf["off"][1];
        if (jf.contains("scale")) f.tex.scaleU = jf["scale"][0], f.tex.scaleV = jf["scale"][1];
        f.tex.rot = jf.value("rot", 0.0f);
        b.faces.push_back(f);
    }
    return b.BuildPolygons();
}

json Map::ToJson() const {
    json j;
    j["format"] = "rbs-map";
    j["version"] = 1;
    const MapSettings& s = settings;
    j["settings"] = {
        {"title", s.title}, {"author", s.author},
        {"sky_zenith", Vec3ToJson(s.skyZenith)}, {"sky_horizon", Vec3ToJson(s.skyHorizon)}, {"sky_clouds", Vec3ToJson(s.skyClouds)},
        {"sun_dir", Vec3ToJson(s.sunDir)}, {"sun_color", Vec3ToJson(s.sunColor)}, {"sun_intensity", s.sunIntensity},
        {"ambient", Vec3ToJson(s.ambient)}, {"fog_color", Vec3ToJson(s.fogColor)}, {"fog_density", s.fogDensity},
        {"lightmap_scale", s.lightmapScale}, {"sky_light", s.skyLight}, {"bounce", s.bounce}, {"ao", s.ao}, {"gravity", s.gravity}, {"music", s.music}, {"next_map", s.nextMap}};
    json world = json::array();
    for (auto& b : brushes)
        if (b.entity < 0) world.push_back(BrushToJson(b));
    j["brushes"] = world;
    json ents = json::array();
    for (size_t i = 0; i < entities.size(); ++i) {
        const MapEntity& e = entities[i];
        json je;
        je["class"] = e.classname;
        je["origin"] = Vec3ToJson(e.origin);
        if (e.angle != 0) je["angle"] = e.angle;
        if (!e.props.empty()) je["props"] = e.props;
        if (e.components.is_array() && !e.components.empty()) je["components"] = e.components;
        json eb = json::array();
        for (auto& b : brushes)
            if (b.entity == (int)i) eb.push_back(BrushToJson(b));
        if (!eb.empty()) je["brushes"] = eb;
        ents.push_back(je);
    }
    j["entities"] = ents;
    return j;
}

bool Map::FromJson(const json& j, std::string* err) {
    Clear();
    try {
        if (j.contains("settings")) {
            const json& js = j["settings"];
            MapSettings& s = settings;
            s.title = js.value("title", s.title);
            s.author = js.value("author", s.author);
            s.skyZenith = JsonToVec3(js.value("sky_zenith", json()), s.skyZenith);
            s.skyHorizon = JsonToVec3(js.value("sky_horizon", json()), s.skyHorizon);
            s.skyClouds = JsonToVec3(js.value("sky_clouds", json()), s.skyClouds);
            s.sunDir = JsonToVec3(js.value("sun_dir", json()), s.sunDir);
            s.sunColor = JsonToVec3(js.value("sun_color", json()), s.sunColor);
            s.sunIntensity = js.value("sun_intensity", s.sunIntensity);
            s.ambient = JsonToVec3(js.value("ambient", json()), s.ambient);
            s.fogColor = JsonToVec3(js.value("fog_color", json()), s.fogColor);
            s.fogDensity = js.value("fog_density", s.fogDensity);
            s.lightmapScale = js.value("lightmap_scale", s.lightmapScale);
            s.skyLight = js.value("sky_light", s.skyLight);
            s.bounce = js.value("bounce", s.bounce);
            s.ao = js.value("ao", s.ao);
            s.gravity = js.value("gravity", s.gravity);
            s.music = js.value("music", s.music);
            s.nextMap = js.value("next_map", s.nextMap);
        }
        if (j.contains("brushes")) {
            for (auto& jb : j["brushes"]) {
                Brush b;
                if (BrushFromJson(jb, b)) brushes.push_back(b);
            }
        }
        if (j.contains("entities")) {
            for (auto& je : j["entities"]) {
                MapEntity e;
                e.classname = je.value("class", std::string("info_null"));
                e.origin = JsonToVec3(je.value("origin", json()), {0, 0, 0});
                e.angle = je.value("angle", 0.0f);
                if (je.contains("props") && je["props"].is_object()) e.props = je["props"];
                if (je.contains("components") && je["components"].is_array()) e.components = je["components"];
                int idx = (int)entities.size();
                entities.push_back(e);
                if (je.contains("brushes")) {
                    for (auto& jb : je["brushes"]) {
                        Brush b;
                        if (BrushFromJson(jb, b)) {
                            b.entity = idx;
                            brushes.push_back(b);
                        }
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        if (err) *err = ex.what();
        return false;
    }
    return true;
}

bool Map::Load(const std::string& relPath, std::string* err) {
    std::string text;
    if (!vfs::ReadText(relPath, text)) {
        if (err) *err = "file not found: " + relPath;
        return false;
    }
    json j = json::parse(text, nullptr, false, true);
    if (j.is_discarded()) {
        if (err) *err = "JSON parse error in " + relPath;
        return false;
    }
    return FromJson(j, err);
}

bool Map::Save(const std::string& relPath) const {
    bool ok = vfs::WriteText(relPath, ToJson().dump(1));
    if (ok) LOG_INFO("Saved %s", relPath.c_str());
    return ok;
}

void Map::RemoveEntity(int idx) {
    if (idx < 0 || idx >= (int)entities.size()) return;
    brushes.erase(std::remove_if(brushes.begin(), brushes.end(), [&](const Brush& b) { return b.entity == idx; }), brushes.end());
    for (auto& b : brushes)
        if (b.entity > idx) b.entity--;
    entities.erase(entities.begin() + idx);
}

}  // namespace rbs
