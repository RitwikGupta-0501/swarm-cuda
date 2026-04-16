#pragma once
#include "agent.h"
#include <cuda_runtime.h>

// GPU-side obstacle struct (no STL, safe to pass to kernels)
struct GPUObstacle {
    int   type;     // 0=circle  1=rect  2=line
    float x, y;
    float x2, y2;
    float radius;
};

void launchBoidsKernel(
    Agent* d_agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex,
    int tableSize, float cellSize,
    float2* renderPositions,

    float separation,
    float alignment,
    float cohesion,

    float perceptionRadius,
    float maxSpeed,
    float maxForce,

    float predatorRatio,
    float predatorSpeedMul,
    float fearWeight,

    float windX,
    float windY,

    bool  attractorActive,
    float attractorX,
    float attractorY,
    float attractorStrength,
    float attractorRadius,

    float speedFactor,

    // obstacle data (may be nullptr when count==0)
    GPUObstacle* d_obstacles,
    int          obstacleCount
);
