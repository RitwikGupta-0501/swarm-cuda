#pragma once
#include <glad/glad.h>

// Initialize GPU simulation
void initSimulation(int agentCount);

// Run simulation step
void stepSimulation(float dt, float mouseX, float mouseY, void* visualizerResource);

// Cleanup GPU resources
void shutdownSimulation();

float* getAgentPositions();
int getAgentCount();
