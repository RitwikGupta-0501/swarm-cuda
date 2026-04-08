#pragma once
#include "agent.h"

// Wrapper to launch CUDA kernel
// Change from the old signature to this:
void launchBoidsKernel(
    Agent* d_agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex,
    int tableSize, float cellSize
);
