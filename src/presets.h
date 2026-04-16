#pragma once
#include "ui.h"
#include <string>

// Built-in preset indices
enum PresetIndex {
    PRESET_BIRD_FLOCK  = 0,
    PRESET_FISH_SCHOOL = 1,
    PRESET_CHAOS       = 2,
    PRESET_ORBITING    = 3,
    PRESET_MURMURATION = 4,
    PRESET_COUNT
};

// Load a built-in preset by index into params
void loadPreset(int presetIndex, SimParams& params);

// Save current params to config/presets.json under 'name'
void savePreset(const std::string& name, const SimParams& params);

// Load a named preset from config/presets.json
bool loadPresetByName(const std::string& name, SimParams& params);
