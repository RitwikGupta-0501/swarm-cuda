#pragma once // it hides CUDA complexity 

// Initialize GPU simulation
void initSimulation(int agentCount);

// Run simulation step
void stepSimulation(float dt, float mouseX, float mouseY);

// Cleanup GPU resources
void shutdownSimulation();

float* getAgentPositions();
int getAgentCount();