#pragma once
#include "ui.h"
#include <vector>

// CPU-side management of obstacles; GPU-side avoidance lives in kernels.cu

// Compute avoidance acceleration for a single agent position/velocity.
// Returns (ax, ay) contribution from all obstacles.
void computeObstacleAvoidance(
    float  px, float  py,
    float  vx, float  vy,
    const  std::vector<Obstacle>& obstacles,
    float  avoidWeight,
    float& outAx, float& outAy);

// Advance any moving obstacles by dt
void updateMovingObstacles(std::vector<Obstacle>& obstacles, float dt);

// Serialize / deserialize obstacles to JSON file
void saveObstacles  (const std::vector<Obstacle>& obstacles, const char* path);
bool loadObstacles  (std::vector<Obstacle>& obstacles, const char* path);
