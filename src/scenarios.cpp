#include "scenarios.h"
#include "presets.h"
#include <cmath>

const char* scenarioName(ScenarioID id) {
    static const char* names[SCENARIO_COUNT] = {
        "Murmuration",
        "Predator Attack",
        "Obstacle Course",
        "Migration"
    };
    return (id >= 0 && id < SCENARIO_COUNT) ? names[id] : "Unknown";
}

// ─── Start ────────────────────────────────────────────────────────────────────
void startScenario(ScenarioID id,
                   SimParams&             params,
                   std::vector<Obstacle>& obstacles,
                   ScenarioState&         state)
{
    state.activeScenario = id;
    state.running        = true;
    state.elapsed        = 0.0f;
    obstacles.clear();

    switch (id) {

    // Murmuration: large flock, tight alignment, gentle cohesion
    case SCENARIO_MURMURATION:
        loadPreset(PRESET_MURMURATION, params);
        params.agentCount       = 15000;
        params.reinitRequested  = true;
        params.predatorRatio    = 0.0f;
        break;

    // Predator Attack: normal flock, then predators spawned centre
    case SCENARIO_PREDATOR_ATCK:
        loadPreset(PRESET_BIRD_FLOCK, params);
        params.agentCount       = 10000;
        params.reinitRequested  = true;
        params.predatorRatio    = 0.05f;
        params.predatorSpeedMul = 2.0f;
        params.fearWeight       = 5.0f;
        break;

    // Obstacle Course: several static circles spread across world
    case SCENARIO_OBSTACLE_CRSE: {
        loadPreset(PRESET_FISH_SCHOOL, params);
        params.agentCount      = 8000;
        params.reinitRequested = true;

        const float positions[][2] = {
            {-0.6f,  0.0f}, { 0.0f,  0.5f}, { 0.5f, -0.3f},
            {-0.3f, -0.5f}, { 0.7f,  0.4f}, {-0.7f,  0.5f}
        };
        for (auto& pos : positions) {
            Obstacle o{};
            o.type   = OBS_CIRCLE;
            o.x      = pos[0];
            o.y      = pos[1];
            o.radius = 0.08f + 0.04f * (float)(std::rand() % 4);
            obstacles.push_back(o);
        }
        break;
    }

    // Migration: attractor point moves across world
    case SCENARIO_MIGRATION:
        loadPreset(PRESET_BIRD_FLOCK, params);
        params.agentCount       = 12000;
        params.reinitRequested  = true;
        params.predatorRatio    = 0.02f;
        params.attractorActive  = true;
        params.attractorX       = -0.8f;
        params.attractorY       =  0.0f;
        params.attractorStrength=  2.0f;
        params.attractorRadius  =  0.4f;
        state.migrationTargetX  =  0.8f;
        state.migrationTargetY  =  0.0f;
        break;

    default: break;
    }
}

// ─── Per-frame update ─────────────────────────────────────────────────────────
void updateScenario(ScenarioState&         state,
                    SimParams&             params,
                    std::vector<Obstacle>& obstacles,
                    float                  dt)
{
    if (!state.running) return;
    state.elapsed += dt;

    switch (state.activeScenario) {

    case SCENARIO_MURMURATION:
        // Slowly rotate wind to create sweeping motion
        params.windX = 0.05f * std::sinf(state.elapsed * 0.3f);
        params.windY = 0.03f * std::cosf(state.elapsed * 0.2f);
        break;

    case SCENARIO_PREDATOR_ATCK:
        // After 5 s, boost fear for dramatic scatter
        if (state.elapsed > 5.0f && state.elapsed < 5.1f)
            params.fearWeight = 8.0f;
        break;

    case SCENARIO_OBSTACLE_CRSE:
        // Slowly move obstacles after 10 s
        if (state.elapsed > 10.0f) {
            for (Obstacle& o : obstacles) {
                if (!o.isMoving) {
                    o.isMoving   = true;
                    o.moveSpeedX = 0.04f * (std::rand() % 3 - 1);
                    o.moveSpeedY = 0.04f * (std::rand() % 3 - 1);
                }
            }
        }
        break;

    case SCENARIO_MIGRATION: {
        // Move attractor across world
        float& ax = params.attractorX;
        float& ay = params.attractorY;
        float dx  = state.migrationTargetX - ax;
        float dy  = state.migrationTargetY - ay;
        float d   = std::sqrtf(dx*dx + dy*dy);
        if (d < 0.05f) {
            // flip direction
            state.migrationTargetX = -state.migrationTargetX;
            state.migrationTargetY = 0.3f * (float)(std::rand() % 3 - 1);
        } else {
            ax += (dx / d) * 0.08f * dt;
            ay += (dy / d) * 0.08f * dt;
        }
        break;
    }

    default: break;
    }
}

// ─── Stop ─────────────────────────────────────────────────────────────────────
void stopScenario(ScenarioState& state, std::vector<Obstacle>& obstacles) {
    state.running  = false;
    state.elapsed  = 0.0f;
    obstacles.clear();
}
