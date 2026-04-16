#pragma once
#include "agent.h"
#include <vector>
#include <string>

// ─── Rendering Options (passed to Person 1) ──────────────────────────────────
enum ColorScheme {
    COLOR_UNIFORM = 0,
    COLOR_VELOCITY,
    COLOR_TYPE,
    COLOR_RAINBOW
};

struct RenderOptions {
    ColorScheme colorScheme   = COLOR_UNIFORM;
    float       agentSize     = 3.0f;
    float       trailLength   = 0.0f;   // 0 = off
    float       cameraFOV     = 45.0f;
    bool        showGrid      = false;
    bool        showVelocity  = false;
};

// ─── Simulation Parameters (passed to Person 2) ──────────────────────────────
struct SimParams {
    float separation        = 1.0f;
    float alignment         = 1.0f;
    float cohesion          = 1.0f;
    float perceptionRadius  = 0.2f;
    float maxSpeed          = 0.5f;
    float maxForce          = 0.1f;
    float speedFactor       = 1.0f;

    // predator-prey
    float predatorRatio     = 0.1f;   // fraction that are predators
    float predatorSpeedMul  = 1.5f;
    float fearWeight        = 3.0f;   // prey fear of predators

    // wind
    float windX             = 0.0f;
    float windY             = 0.0f;

    // attractor
    bool  attractorActive   = false;
    float attractorX        = 0.0f;
    float attractorY        = 0.0f;
    float attractorStrength = 1.0f;   // positive = attract, negative = repel
    float attractorRadius   = 0.3f;

    int   agentCount        = 10000;
    bool  reinitRequested   = false;  // set when count changes
};

// ─── Obstacle ────────────────────────────────────────────────────────────────
enum ObstacleType { OBS_CIRCLE = 0, OBS_RECT, OBS_LINE };

struct Obstacle {
    ObstacleType type = OBS_CIRCLE;
    float x  = 0.0f, y  = 0.0f;   // centre / start point
    float x2 = 0.0f, y2 = 0.0f;   // end point (line) / half-extents (rect)
    float radius = 0.1f;           // circle radius
    bool  isMoving = false;
    float moveSpeedX = 0.0f, moveSpeedY = 0.0f;
};

// ─── Statistics (read each frame, computed externally or approximated) ────────
struct SimStats {
    float fps           = 0.0f;
    float frameTimeMs   = 0.0f;
    float simTimeMs     = 0.0f;
    float renderTimeMs  = 0.0f;
    float avgSpeed      = 0.0f;
    int   predatorCount = 0;
    int   preyCount     = 0;
};

// ─── Public API ──────────────────────────────────────────────────────────────
void renderUI(bool& paused, float& speed,
              float& separation, float& alignment, float& cohesion);

// Full-featured version used when all sub-systems are integrated
void renderFullUI(SimParams&          params,
                  RenderOptions&      renderOpts,
                  SimStats&           stats,
                  std::vector<Obstacle>& obstacles,
                  bool&               paused,
                  bool&               screenshotRequested,
                  bool&               recordingActive);
