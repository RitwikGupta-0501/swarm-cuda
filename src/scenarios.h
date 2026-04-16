#pragma once
#include "ui.h"
#include <vector>
#include <string>

// ─── Scenario IDs ─────────────────────────────────────────────────────────────
enum ScenarioID {
    SCENARIO_MURMURATION   = 0,
    SCENARIO_PREDATOR_ATCK = 1,
    SCENARIO_OBSTACLE_CRSE = 2,
    SCENARIO_MIGRATION     = 3,
    SCENARIO_COUNT
};

// ─── Scenario state ───────────────────────────────────────────────────────────
struct ScenarioState {
    ScenarioID         activeScenario   = SCENARIO_MURMURATION;
    bool               running          = false;
    float              elapsed          = 0.0f;   // seconds since start
    // Migration target: moves over time
    float              migrationTargetX = 0.8f;
    float              migrationTargetY = 0.0f;
};

// Start a scenario: sets params, obstacles, etc.
void startScenario(ScenarioID id,
                   SimParams&            params,
                   std::vector<Obstacle>& obstacles,
                   ScenarioState&         state);

// Called every frame while a scenario is active
void updateScenario(ScenarioState&         state,
                    SimParams&             params,
                    std::vector<Obstacle>& obstacles,
                    float                  dt);

// Stop / clean up current scenario
void stopScenario(ScenarioState& state,
                  std::vector<Obstacle>& obstacles);

// Returns display name for a scenario
const char* scenarioName(ScenarioID id);
