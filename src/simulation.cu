#include "simulation.h"
#include "kernels.h"
#include "agent.h"
#include "spatial_hash.h"
#include "interop_cuda_gl.h"

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <cstdlib>
#include <string>

static Agent* d_agents = nullptr;
static Agent* h_agents = nullptr;
static int agentCountGlobal = 0;

static SpatialHash sh;

// 🔥 INIT
void initSimulation(int agentCount) {
    agentCountGlobal = agentCount;

    h_agents = new Agent[agentCount];

    for (int i = 0; i < agentCount; i++) {
        h_agents[i].x = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        h_agents[i].y = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;

        h_agents[i].vx = 0;
        h_agents[i].vy = 0;
        h_agents[i].ax = 0;
        h_agents[i].ay = 0;

        h_agents[i].max_speed = 0.5f;
        h_agents[i].perception_radius = 0.2f;

        // 🔥 predator / prey assignment
        if (i < agentCount * 0.9f) {
            h_agents[i].type = PREY;       // 90% prey
        } else {
            h_agents[i].type = PREDATOR;   // 10% predators
            h_agents[i].max_speed = 1.0f;  // predators faster
        }
    }

    cudaMalloc(&d_agents, agentCount * sizeof(Agent));
    cudaMemcpy(d_agents, h_agents, agentCount * sizeof(Agent), cudaMemcpyHostToDevice);

    initSpatialHash(sh, agentCount);
}

// 🔁 STEP
void stepSimulation(float dt, float mouseX, float mouseY, void* visualizerResource) {
    RenderAgent* d_positions = nullptr;

    if (visualizerResource != nullptr) {
        size_t mappedSize = 0;
        std::string err;
        swarm::cudaMapAgentBuffer(
            static_cast<cudaGraphicsResource*>(visualizerResource),
            reinterpret_cast<void**>(&d_positions),
            &mappedSize,
            &err
        );
    }

    buildSpatialHash(sh, d_agents, agentCountGlobal, 0.2f);

    launchBoidsKernel(
        d_agents,
        agentCountGlobal,
        dt,
        mouseX,
        mouseY,
        sh.cell_start,
        sh.cell_end,
        sh.sorted_agents,
        sh.table_size,
        sh.cell_size,
        d_positions
    );

    cudaDeviceSynchronize();

    if (visualizerResource != nullptr) {
        std::string err;
        swarm::cudaUnmapAgentBuffer(
            static_cast<cudaGraphicsResource*>(visualizerResource),
            &err
        );
    } else {
        cudaMemcpy(h_agents, d_agents, agentCountGlobal * sizeof(Agent), cudaMemcpyDeviceToHost);
    }
}

// 🧹 CLEANUP
void shutdownSimulation() {
    cudaFree(d_agents);
    delete[] h_agents;
    destroySpatialHash(sh);
}

// 🎨 GET POSITIONS FOR RENDERING
float* getAgentPositions() {
    static float* positions = nullptr;

    if (!positions)
        positions = new float[agentCountGlobal * 2];

    for (int i = 0; i < agentCountGlobal; i++) {
        positions[2*i]     = h_agents[i].x;
        positions[2*i + 1] = h_agents[i].y;
    }

    return positions;
}

// 🔢 GET COUNT
int getAgentCount() {
    return agentCountGlobal;
}
