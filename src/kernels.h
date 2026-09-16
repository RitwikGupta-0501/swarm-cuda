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

struct SimConfig {
    float separation;
    float alignment;
    float cohesion;
    float perceptionRadius;
    float maxSpeed;
    float maxForce;
    float predatorRatio;
    float predatorSpeedMul;
    float fearWeight;
    float windX;
    float windY;
    bool  attractorActive;
    float attractorX;
    float attractorY;
    float attractorStrength;
    float attractorRadius;
    float speedFactor;
    
    // Magic numbers replaced
    float lookAhead;
    float safetyDist;
    float avoidWeight;
    float sepWeight;
    float aliWeight;
    float cohWeight;
    float boundaryTurnFactor;
    float boundaryMargin;
};

void uploadSimConfig(const SimConfig& config);

void launchBoidsKernel(
    Agent* d_agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex, const Agent* sorted_agents_data,
    int tableSize, float cellSize,
    float4* renderPositions,
    GPUObstacle* d_obstacles,
    int obstacleCount
);