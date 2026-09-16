#pragma once

#include <glad/glad.h>
#include "ui.h"

#include <vector>
#include "ui.h"

// Forward declaration of internal CUDA types if needed
class SwarmEngine {
public:
    SwarmEngine() = default;
    ~SwarmEngine() { shutdown(); }

    // Initialize GPU simulation
    void init(int agentCount, const SimParams& params);

    // Run simulation step
    void step(float dt, float mouseX, float mouseY, const SimParams& params);

    // CUDA/OpenGL interop for particle rendering buffer
    void registerRenderBuffer(GLuint vbo);
    void unregisterRenderBuffer();

    void updateGPUObstacles(const std::vector<Obstacle>& obs);

    // Agent stats
    void getCounts(int* predators, int* prey);
    float getAverageSpeed();
    float* getAgentPositions();
    int getAgentCount() const;
    void getKernelProfileTimes(float& hashTimeMs, float& kernelTimeMs);

    // Add a single agent of given type at random position
    void addAgent(int type);   // type: PREY=0, PREDATOR=1

    // Randomly convert one prey → predator (or vice-versa)
    void convertRandomAgent();

    // Cleanup GPU resources
    void shutdown();

    // Check if agent count changed
    bool isAgentCountDirty() const;
    void clearAgentCountDirty();

private:
    struct State;
    State* state = nullptr;
};
