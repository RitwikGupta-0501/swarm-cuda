#pragma once
#include "agent.h"
#include <cuda_runtime.h>

// Define the 16-byte structure expected by the visualizer
struct RenderAgent {
    float px, py;
    float vx, vy;
};

// Wrapper to launch CUDA kernel
// Change from the old signature to this:
void launchBoidsKernel(
    Agent* d_agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex,
    int tableSize, float cellSize,
    RenderAgent* renderPositions
);
