#include "presets.h"
#include <fstream>
#include <cstdio>
#include <cstring>
#include <direct.h>   // _mkdir on Windows
 
// ─── Built-in preset data ─────────────────────────────────────────────────────
struct PresetData {
    const char* name;
    float separation, alignment, cohesion;
    float perceptionRadius, maxSpeed, maxForce, speedFactor;
    float predatorRatio, predatorSpeedMul, fearWeight;
};
 
static const PresetData PRESETS[PRESET_COUNT] = {
    // name           sep   ali   coh   percR  mxSpd  mxF    spd   predR  predMul  fear
    { "Bird Flock",   1.2f, 2.5f, 1.0f, 0.20f, 0.50f, 0.10f, 1.0f, 0.05f, 1.5f,  2.0f },
    { "Fish School",  1.5f, 1.5f, 3.0f, 0.15f, 0.40f, 0.08f, 1.0f, 0.05f, 1.5f,  3.0f },
    { "Chaos",        4.0f, 0.2f, 0.2f, 0.25f, 1.00f, 0.15f, 1.5f, 0.10f, 2.0f,  1.0f },
    { "Orbiting",     0.5f, 4.0f, 4.0f, 0.30f, 0.35f, 0.05f, 0.8f, 0.00f, 1.5f,  1.0f },
    { "Murmuration",  1.8f, 3.5f, 1.2f, 0.22f, 0.55f, 0.12f, 1.2f, 0.03f, 1.8f,  2.5f },
};
 
void loadPreset(int idx, SimParams& p) {
    if (idx < 0 || idx >= PRESET_COUNT) return;
    const PresetData& d = PRESETS[idx];
    p.separation       = d.separation;
    p.alignment        = d.alignment;
    p.cohesion         = d.cohesion;
    p.perceptionRadius = d.perceptionRadius;
    p.maxSpeed         = d.maxSpeed;
    p.maxForce         = d.maxForce;
    p.speedFactor      = d.speedFactor;
    p.predatorRatio    = d.predatorRatio;
    p.predatorSpeedMul = d.predatorSpeedMul;
    p.fearWeight       = d.fearWeight;
}
 
// ─── Minimal JSON helpers (no external deps) ─────────────────────────────────
static std::string floatField(const char* key, float val) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "  \"%s\": %.6f", key, val);
    return std::string(buf);
}
static std::string intField(const char* key, int val) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "  \"%s\": %d", key, val);
    return std::string(buf);
}
 
void savePreset(const std::string& name, const SimParams& p) {
    // Windows-compatible directory creation
    _mkdir("config");
 
    std::ofstream f("config/presets.json", std::ios::app);
    if (!f.is_open()) return;
 
    f << "{\n";
    f << "  \"name\": \"" << name << "\",\n";
    f << floatField("separation",       p.separation)       << ",\n";
    f << floatField("alignment",        p.alignment)        << ",\n";
    f << floatField("cohesion",         p.cohesion)         << ",\n";
    f << floatField("perceptionRadius", p.perceptionRadius) << ",\n";
    f << floatField("maxSpeed",         p.maxSpeed)         << ",\n";
    f << floatField("maxForce",         p.maxForce)         << ",\n";
    f << floatField("speedFactor",      p.speedFactor)      << ",\n";
    f << floatField("predatorRatio",    p.predatorRatio)    << ",\n";
    f << floatField("predatorSpeedMul", p.predatorSpeedMul) << ",\n";
    f << floatField("fearWeight",       p.fearWeight)       << ",\n";
    f << intField  ("agentCount",       p.agentCount)       << "\n";
    f << "}\n";
}
 
bool loadPresetByName(const std::string& name, SimParams& p) {
    std::ifstream f("config/presets.json");
    if (!f.is_open()) return false;
 
    bool inBlock     = false;
    bool nameMatched = false;
    SimParams tmp;
    std::string line;
 
    auto parseFloat = [](const std::string& ln, const char* key, float& out) {
        std::string k = std::string("\"") + key + "\": ";
        auto pos = ln.find(k);
        if (pos == std::string::npos) return false;
        out = std::stof(ln.substr(pos + k.size()));
        return true;
    };
    auto parseInt = [](const std::string& ln, const char* key, int& out) {
        std::string k = std::string("\"") + key + "\": ";
        auto pos = ln.find(k);
        if (pos == std::string::npos) return false;
        out = std::stoi(ln.substr(pos + k.size()));
        return true;
    };
 
    while (std::getline(f, line)) {
        if (line.find("{") != std::string::npos) {
            inBlock = true; nameMatched = false; tmp = SimParams{};
        }
        if (!inBlock) continue;
 
        if (line.find("\"name\"") != std::string::npos &&
            line.find(name)       != std::string::npos)
            nameMatched = true;
 
        parseFloat(line, "separation",       tmp.separation);
        parseFloat(line, "alignment",        tmp.alignment);
        parseFloat(line, "cohesion",         tmp.cohesion);
        parseFloat(line, "perceptionRadius", tmp.perceptionRadius);
        parseFloat(line, "maxSpeed",         tmp.maxSpeed);
        parseFloat(line, "maxForce",         tmp.maxForce);
        parseFloat(line, "speedFactor",      tmp.speedFactor);
        parseFloat(line, "predatorRatio",    tmp.predatorRatio);
        parseFloat(line, "predatorSpeedMul", tmp.predatorSpeedMul);
        parseFloat(line, "fearWeight",       tmp.fearWeight);
        parseInt  (line, "agentCount",       tmp.agentCount);
 
        if (line.find("}") != std::string::npos) {
            if (nameMatched) { p = tmp; return true; }
            inBlock = false;
        }
    }
    return false;
}
 