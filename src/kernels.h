#pragma once
#include "agent.h"

// Wrapper to launch CUDA kernel
void launchBoidsKernel(Agent* d_agents, int count, float dt, float mouseX, float mouseY);