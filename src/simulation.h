#pragma once

#include <glad/glad.h>
#include "ui.h"

// Initialize GPU simulation
void initSimulation(int agentCount, const SimParams& params);

// Run simulation step
void stepSimulation(float dt, float mouseX, float mouseY,
                    const SimParams& params);

// CUDA/OpenGL interop for particle rendering buffer
void registerRenderBuffer(GLuint vbo);
void unregisterRenderBuffer();

// Agent stats
void getCounts(int* predators, int* prey);
float getAverageSpeed();
float* getAgentPositions();
int getAgentCount();

// Add a single agent of given type at random position
void addAgent(int type);   // type: PREY=0, PREDATOR=1

// Randomly convert one prey → predator (or vice-versa)
void convertRandomAgent();

// Cleanup GPU resources
void shutdownSimulation();
