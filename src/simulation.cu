#include "simulation.h"
#include "kernels.h"
#include "agent.h"
#include "spatial_hash.h"

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <cstdlib>
#include <cmath>
#include <algorithm>

static Agent* d_agents  = nullptr;
static Agent* h_agents  = nullptr;
static int    agentCountGlobal = 0;
static cudaGraphicsResource* renderResource = nullptr;
static SpatialHash sh;

// ─── GPU obstacle buffer ──────────────────────────────────────────────────────
// Mirrors the CPU Obstacle list each frame so the kernel can use it.
// We use a plain struct that is CUDA-friendly (no vtable, no std::).


static GPUObstacle* d_obstacles    = nullptr;
static int          d_obstaclesCap = 0;
static int          d_obstaclesN   = 0;

static void uploadObstacles(const std::vector<Obstacle>& obs)
{
    int n = (int)obs.size();
    d_obstaclesN = n;
    if (n == 0) return;

    if (n > d_obstaclesCap) {
        if (d_obstacles) cudaFree(d_obstacles);
        cudaMalloc(&d_obstacles, n * sizeof(GPUObstacle));
        d_obstaclesCap = n;
    }

    // fill temp host buffer
    static std::vector<GPUObstacle> tmp;
    tmp.resize(n);
    for (int i = 0; i < n; i++) {
        tmp[i] = { (int)obs[i].type, obs[i].x, obs[i].y,
                   obs[i].x2, obs[i].y2, obs[i].radius };
    }
    cudaMemcpy(d_obstacles, tmp.data(), n * sizeof(GPUObstacle),
               cudaMemcpyHostToDevice);
}

// ─── INIT ─────────────────────────────────────────────────────────────────────
void initSimulation(int agentCount, const SimParams& params)
{
    if (h_agents) { delete[] h_agents; h_agents = nullptr; }
    if (d_agents) { cudaFree(d_agents); d_agents = nullptr; }

    agentCountGlobal = agentCount;
    h_agents = new Agent[agentCount];

    for (int i = 0; i < agentCount; i++) {
        h_agents[i].x  = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        h_agents[i].y  = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        h_agents[i].vx = ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
        h_agents[i].vy = ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
        h_agents[i].ax = 0; h_agents[i].ay = 0;
        h_agents[i].max_speed        = params.maxSpeed;
        h_agents[i].perception_radius= params.perceptionRadius;

        if ((float)i / agentCount < params.predatorRatio) {
            h_agents[i].type      = PREDATOR;
            h_agents[i].max_speed = params.maxSpeed * params.predatorSpeedMul;
        } else {
            h_agents[i].type = PREY;
        }
    }

    cudaMalloc(&d_agents, agentCount * sizeof(Agent));
    cudaMemcpy(d_agents, h_agents, agentCount * sizeof(Agent),
               cudaMemcpyHostToDevice);

    initSpatialHash(sh, agentCount);
}

// ─── ADD / CONVERT ────────────────────────────────────────────────────────────
void addAgent(int type)
{
    // Grow host array by 1
    Agent* newH = new Agent[agentCountGlobal + 1];
    memcpy(newH, h_agents, agentCountGlobal * sizeof(Agent));
    Agent& a = newH[agentCountGlobal];
    a.x  = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
    a.y  = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
    a.vx = ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
    a.vy = ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
    a.ax = 0; a.ay = 0;
    a.type              = type;
    a.max_speed         = 0.5f;
    a.perception_radius = 0.2f;
    delete[] h_agents;
    h_agents = newH;

    // Reallocate GPU
    Agent* newD = nullptr;
    cudaMalloc(&newD, (agentCountGlobal + 1) * sizeof(Agent));
    cudaMemcpy(newD, h_agents, (agentCountGlobal + 1) * sizeof(Agent),
               cudaMemcpyHostToDevice);
    cudaFree(d_agents);
    d_agents = newD;

    agentCountGlobal++;
    destroySpatialHash(sh);
    initSpatialHash(sh, agentCountGlobal);
}

void convertRandomAgent()
{
    if (agentCountGlobal == 0) return;
    int idx = rand() % agentCountGlobal;
    h_agents[idx].type = (h_agents[idx].type == PREY) ? PREDATOR : PREY;
    cudaMemcpy(d_agents + idx, h_agents + idx, sizeof(Agent),
               cudaMemcpyHostToDevice);
}

// ─── STEP ─────────────────────────────────────────────────────────────────────
void stepSimulation(float dt, float mouseX, float mouseY,
                    const SimParams& params)
{
    float2* d_positions = nullptr;

    if (renderResource != nullptr) {
        cudaGraphicsMapResources(1, &renderResource, 0);
        size_t mappedSize = 0;
        cudaGraphicsResourceGetMappedPointer(
            reinterpret_cast<void**>(&d_positions),
            &mappedSize, renderResource);
    }

    buildSpatialHash(sh, d_agents, agentCountGlobal, params.perceptionRadius);

    launchBoidsKernel(
        d_agents, agentCountGlobal, dt, mouseX, mouseY,
        sh.cell_start, sh.cell_end, sh.sorted_agents,
        sh.table_size, sh.cell_size, d_positions,
        params.separation, params.alignment, params.cohesion,
        params.perceptionRadius, params.maxSpeed, params.maxForce,
        params.predatorRatio, params.predatorSpeedMul, params.fearWeight,
        params.windX, params.windY,
        params.attractorActive,
        params.attractorX, params.attractorY,
        params.attractorStrength, params.attractorRadius,
        params.speedFactor,
        d_obstacles, d_obstaclesN
    );

    cudaDeviceSynchronize();

    if (renderResource != nullptr)
        cudaGraphicsUnmapResources(1, &renderResource, 0);

    cudaMemcpy(h_agents, d_agents,
               agentCountGlobal * sizeof(Agent), cudaMemcpyDeviceToHost);
}

// Called each frame from main before stepSimulation so GPU obstacles stay in sync
void updateGPUObstacles(const std::vector<Obstacle>& obs)
{
    uploadObstacles(obs);
}

// ─── INTEROP ──────────────────────────────────────────────────────────────────
void registerRenderBuffer(GLuint vbo)
{
    if (renderResource) unregisterRenderBuffer();
    cudaGraphicsGLRegisterBuffer(&renderResource, vbo,
                                 cudaGraphicsRegisterFlagsWriteDiscard);
}

void unregisterRenderBuffer()
{
    if (renderResource) {
        cudaGraphicsUnregisterResource(renderResource);
        renderResource = nullptr;
    }
}

// ─── STATS ────────────────────────────────────────────────────────────────────
float getAverageSpeed()
{
    if (agentCountGlobal == 0 || !h_agents) return 0.0f;
    float total = 0.0f;
    for (int i = 0; i < agentCountGlobal; i++) {
        float vx = h_agents[i].vx, vy = h_agents[i].vy;
        total += sqrtf(vx*vx + vy*vy);
    }
    return total / agentCountGlobal;
}

void getCounts(int* predators, int* prey)
{
    int p = 0, pr = 0;
    for (int i = 0; i < agentCountGlobal; i++) {
        if (h_agents[i].type == PREDATOR) p++; else pr++;
    }
    *predators = p; *prey = pr;
}

float* getAgentPositions()
{
    static float* positions = nullptr;
    static int    posCap    = 0;
    if (agentCountGlobal > posCap) {
        delete[] positions;
        positions = new float[agentCountGlobal * 2];
        posCap = agentCountGlobal;
    }
    for (int i = 0; i < agentCountGlobal; i++) {
        positions[2*i]   = h_agents[i].x;
        positions[2*i+1] = h_agents[i].y;
    }
    return positions;
}

int getAgentCount() { return agentCountGlobal; }

// ─── SHUTDOWN ─────────────────────────────────────────────────────────────────
void shutdownSimulation()
{
    unregisterRenderBuffer();
    if (d_agents) { cudaFree(d_agents); d_agents = nullptr; }
    if (h_agents) { delete[] h_agents;  h_agents = nullptr; }
    if (d_obstacles) { cudaFree(d_obstacles); d_obstacles = nullptr; d_obstaclesCap = 0; }
    destroySpatialHash(sh);
}
