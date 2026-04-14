#pragma once // it hides CUDA complexity 

#include <glad/glad.h>

// Initialize GPU simulation
void initSimulation(int agentCount);

// Run simulation step
void stepSimulation(float dt, float mouseX, float mouseY);

// CUDA/OpenGL interop for particle rendering buffer
void registerRenderBuffer(GLuint vbo);
void unregisterRenderBuffer();

// Cleanup GPU resources
void shutdownSimulation();

float* getAgentPositions();
int getAgentCount();
