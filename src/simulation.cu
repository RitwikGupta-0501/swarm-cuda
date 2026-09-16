#include "simulation.h"
#include "kernels.h"
#include "agent.h"
#include "spatial_hash.h"

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <thrust/device_ptr.h>
#include <thrust/reduce.h>
#include <thrust/count.h>
#include <thrust/execution_policy.h>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <random>
#include <stdexcept>

#define CUDA_CHECK(call)                                                      \
    do {                                                                      \
        cudaError_t err = (call);                                             \
        if (err != cudaSuccess) {                                             \
            fprintf(stderr, "CUDA error at %s:%d - %s\n",                     \
                    __FILE__, __LINE__, cudaGetErrorString(err));             \
            throw std::runtime_error(cudaGetErrorString(err));                \
        }                                                                     \
    } while (0)

struct SwarmEngine::State {
    Agent* d_agents = nullptr;
    Agent* h_agents = nullptr; // Used for getAgentPositions (rendering needs it sometimes, but wait, positions are given to OpenGL VBO)
    // Actually, getAgentPositions is used for what? Maybe JSON export?
    int agentCount = 0;
    int agentCapacity = 200000; // Fixed large capacity
    bool agentCountDirty = false;
    bool hostDirty = true;
    
    cudaGraphicsResource* renderResource = nullptr;
    SpatialHash sh;
    
    float hashTimeMs = 0.0f;
    float kernelTimeMs = 0.0f;
    cudaEvent_t startEvent = nullptr;
    cudaEvent_t stopEvent = nullptr;
    
    GPUObstacle* d_obstacles = nullptr;
    GPUObstacle* h_pinned_obstacles = nullptr;
    int obstaclesCap = 0;
    int obstaclesN = 0;
    
    std::mt19937 rng{42};
    std::uniform_real_distribution<float> dist01{0.0f, 1.0f};
    
    float randFloat() { return dist01(rng); }
    float randFloat(float lo, float hi) { return lo + (hi - lo) * randFloat(); }
    
    void syncHostAgents() {
        if (!hostDirty || !d_agents || !h_agents) return;
        CUDA_CHECK(cudaMemcpy(h_agents, d_agents, agentCount * sizeof(Agent), cudaMemcpyDeviceToHost));
        hostDirty = false;
    }
};

void SwarmEngine::init(int agentCount, const SimParams& params) {
    if (!state) state = new State();
    
    shutdown(); // clear if existing
    
    state->agentCount = agentCount;
    state->agentCapacity = std::max(agentCount * 2, 200000); // at least 200k
    
    state->h_agents = new Agent[state->agentCapacity];
    for (int i = 0; i < agentCount; i++) {
        state->h_agents[i].x  = state->randFloat(-1.0f, 1.0f);
        state->h_agents[i].y  = state->randFloat(-1.0f, 1.0f);
        state->h_agents[i].vx = state->randFloat(-0.5f, 0.5f) * 0.2f;
        state->h_agents[i].vy = state->randFloat(-0.5f, 0.5f) * 0.2f;
        state->h_agents[i].max_speed        = params.maxSpeed;
        state->h_agents[i].perception_radius= params.perceptionRadius;

        if ((float)i / agentCount < params.predatorRatio) {
            state->h_agents[i].type      = PREDATOR;
            state->h_agents[i].max_speed = params.maxSpeed * params.predatorSpeedMul;
        } else {
            state->h_agents[i].type = PREY;
        }
    }
    
    CUDA_CHECK(cudaMalloc(&state->d_agents, state->agentCapacity * sizeof(Agent)));
    CUDA_CHECK(cudaMemcpy(state->d_agents, state->h_agents, agentCount * sizeof(Agent), cudaMemcpyHostToDevice));
    
    CUDA_CHECK(cudaEventCreate(&state->startEvent));
    CUDA_CHECK(cudaEventCreate(&state->stopEvent));
    
    initSpatialHash(state->sh, state->agentCapacity);
    
    // Allocate config and upload
    SimConfig cfg;
    cfg.separation = params.separation;
    cfg.alignment = params.alignment;
    cfg.cohesion = params.cohesion;
    cfg.perceptionRadius = params.perceptionRadius;
    cfg.maxSpeed = params.maxSpeed;
    cfg.maxForce = params.maxForce;
    cfg.predatorRatio = params.predatorRatio;
    cfg.predatorSpeedMul = params.predatorSpeedMul;
    cfg.fearWeight = params.fearWeight;
    cfg.windX = params.windX;
    cfg.windY = params.windY;
    cfg.attractorActive = params.attractorActive;
    cfg.attractorX = params.attractorX;
    cfg.attractorY = params.attractorY;
    cfg.attractorStrength = params.attractorStrength;
    cfg.attractorRadius = params.attractorRadius;
    cfg.speedFactor = params.speedFactor;
    cfg.lookAhead = 0.15f;
    cfg.safetyDist = 0.08f;
    cfg.avoidWeight = 3.0f;
    cfg.sepWeight = 4.0f;
    cfg.aliWeight = 2.0f;
    cfg.cohWeight = 1.5f;
    cfg.boundaryTurnFactor = 0.5f;
    cfg.boundaryMargin = 0.9f;
    uploadSimConfig(cfg);
}

void SwarmEngine::addAgent(int type) {
    if (!state || state->agentCount >= state->agentCapacity) return; // Prevent overflow silently for now
    
    Agent a;
    a.x  = state->randFloat(-1.0f, 1.0f);
    a.y  = state->randFloat(-1.0f, 1.0f);
    a.vx = state->randFloat(-0.5f, 0.5f) * 0.2f;
    a.vy = state->randFloat(-0.5f, 0.5f) * 0.2f;
    a.type              = type;
    a.max_speed         = 0.5f; // would be better to fetch from params, but keeping old logic
    a.perception_radius = 0.2f;

    CUDA_CHECK(cudaMemcpy(state->d_agents + state->agentCount, &a, sizeof(Agent), cudaMemcpyHostToDevice));
    
    state->agentCount++;
    state->agentCountDirty = true;
    state->hostDirty = true;
}

void SwarmEngine::convertRandomAgent() {
    if (!state || state->agentCount == 0) return;
    state->syncHostAgents();
    std::uniform_int_distribution<int> dist(0, state->agentCount - 1);
    int idx = dist(state->rng);
    state->h_agents[idx].type = (state->h_agents[idx].type == PREY) ? PREDATOR : PREY;
    CUDA_CHECK(cudaMemcpy(state->d_agents + idx, state->h_agents + idx, sizeof(Agent), cudaMemcpyHostToDevice));
}

void SwarmEngine::step(float dt, float mouseX, float mouseY, const SimParams& params) {
    if (!state) return;
    
    // Upload config
    SimConfig cfg;
    cfg.separation = params.separation;
    cfg.alignment = params.alignment;
    cfg.cohesion = params.cohesion;
    cfg.perceptionRadius = params.perceptionRadius;
    cfg.maxSpeed = params.maxSpeed;
    cfg.maxForce = params.maxForce;
    cfg.predatorRatio = params.predatorRatio;
    cfg.predatorSpeedMul = params.predatorSpeedMul;
    cfg.fearWeight = params.fearWeight;
    cfg.windX = params.windX;
    cfg.windY = params.windY;
    cfg.attractorActive = params.attractorActive;
    cfg.attractorX = params.attractorX;
    cfg.attractorY = params.attractorY;
    cfg.attractorStrength = params.attractorStrength;
    cfg.attractorRadius = params.attractorRadius;
    cfg.speedFactor = params.speedFactor;
    cfg.lookAhead = 0.15f;
    cfg.safetyDist = 0.08f;
    cfg.avoidWeight = 3.0f;
    cfg.sepWeight = 4.0f;
    cfg.aliWeight = 2.0f;
    cfg.cohWeight = 1.5f;
    cfg.boundaryTurnFactor = 0.5f;
    cfg.boundaryMargin = 0.9f;
    uploadSimConfig(cfg);

    float4* d_positions = nullptr;
    if (state->renderResource != nullptr) {
        CUDA_CHECK(cudaGraphicsMapResources(1, &state->renderResource, 0));
        size_t mappedSize = 0;
        CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(
            reinterpret_cast<void**>(&d_positions), &mappedSize, state->renderResource));
    }

    cudaEventRecord(state->startEvent);
    buildSpatialHash(state->sh, state->d_agents, state->agentCount, params.perceptionRadius);
    cudaEventRecord(state->stopEvent);
    cudaEventSynchronize(state->stopEvent);
    cudaEventElapsedTime(&state->hashTimeMs, state->startEvent, state->stopEvent);

    cudaEventRecord(state->startEvent);
    launchBoidsKernel(
        state->d_agents, state->agentCount, dt, mouseX, mouseY,
        state->sh.cell_start, state->sh.cell_end, state->sh.sorted_agents, state->sh.sorted_agents_data,
        state->sh.table_size, state->sh.cell_size, d_positions,
        state->d_obstacles, state->obstaclesN
    );
    cudaEventRecord(state->stopEvent);
    cudaEventSynchronize(state->stopEvent);
    cudaEventElapsedTime(&state->kernelTimeMs, state->startEvent, state->stopEvent);

    if (state->renderResource != nullptr)
        CUDA_CHECK(cudaGraphicsUnmapResources(1, &state->renderResource, 0));

    state->hostDirty = true;
}

void SwarmEngine::updateGPUObstacles(const std::vector<Obstacle>& obs) {
    if (!state) return;
    int n = (int)obs.size();
    state->obstaclesN = n;
    if (n == 0) return;

    if (n > state->obstaclesCap) {
        if (state->d_obstacles) {
            CUDA_CHECK(cudaFree(state->d_obstacles));
            CUDA_CHECK(cudaFreeHost(state->h_pinned_obstacles));
        }
        state->obstaclesCap = std::max(n, state->obstaclesCap * 2 + 16);
        CUDA_CHECK(cudaMalloc(&state->d_obstacles, state->obstaclesCap * sizeof(GPUObstacle)));
        CUDA_CHECK(cudaMallocHost(&state->h_pinned_obstacles, state->obstaclesCap * sizeof(GPUObstacle)));
    }

    for (int i = 0; i < n; i++) {
        state->h_pinned_obstacles[i] = { (int)obs[i].type, obs[i].x, obs[i].y, obs[i].x2, obs[i].y2, obs[i].radius };
    }
    CUDA_CHECK(cudaMemcpyAsync(state->d_obstacles, state->h_pinned_obstacles, n * sizeof(GPUObstacle), cudaMemcpyHostToDevice));
}

void SwarmEngine::registerRenderBuffer(GLuint vbo) {
    if (!state) return;
    if (state->renderResource) unregisterRenderBuffer();
    CUDA_CHECK(cudaGraphicsGLRegisterBuffer(&state->renderResource, vbo, cudaGraphicsRegisterFlagsWriteDiscard));
}

void SwarmEngine::unregisterRenderBuffer() {
    if (!state || !state->renderResource) return;
    CUDA_CHECK(cudaGraphicsUnregisterResource(state->renderResource));
    state->renderResource = nullptr;
}

// Thrust functors
struct SpeedFunctor {
    __device__ float operator()(const Agent& a) const {
        return sqrtf(a.vx * a.vx + a.vy * a.vy);
    }
};

struct IsPredator {
    __device__ bool operator()(const Agent& a) const {
        return a.type == 1; // PREDATOR
    }
};

float SwarmEngine::getAverageSpeed() {
    if (!state || state->agentCount == 0) return 0.0f;
    thrust::device_ptr<Agent> dev_ptr(state->d_agents);
    float total = thrust::transform_reduce(thrust::device, dev_ptr, dev_ptr + state->agentCount, SpeedFunctor(), 0.0f, thrust::plus<float>());
    return total / state->agentCount;
}

void SwarmEngine::getCounts(int* predators, int* prey) {
    if (!state || state->agentCount == 0) {
        *predators = 0; *prey = 0;
        return;
    }
    thrust::device_ptr<Agent> dev_ptr(state->d_agents);
    int p = thrust::count_if(thrust::device, dev_ptr, dev_ptr + state->agentCount, IsPredator());
    *predators = p;
    *prey = state->agentCount - p;
}

float* SwarmEngine::getAgentPositions() {
    if (!state) return nullptr;
    state->syncHostAgents();
    static float* positions = nullptr;
    static int posCap = 0;
    if (state->agentCount > posCap) {
        delete[] positions;
        positions = new float[state->agentCapacity * 2];
        posCap = state->agentCapacity;
    }
    for (int i = 0; i < state->agentCount; i++) {
        positions[2*i]   = state->h_agents[i].x;
        positions[2*i+1] = state->h_agents[i].y;
    }
    return positions;
}

int SwarmEngine::getAgentCount() const { return state ? state->agentCount : 0; }
void SwarmEngine::getKernelProfileTimes(float& hashTimeMs, float& kernelTimeMs) {
    hashTimeMs = state ? state->hashTimeMs : 0.0f;
    kernelTimeMs = state ? state->kernelTimeMs : 0.0f;
}
bool SwarmEngine::isAgentCountDirty() const { return state ? state->agentCountDirty : false; }
void SwarmEngine::clearAgentCountDirty() { if (state) state->agentCountDirty = false; }

void SwarmEngine::shutdown() {
    if (!state) return;
    unregisterRenderBuffer();
    if (state->startEvent) { cudaEventDestroy(state->startEvent); state->startEvent = nullptr; }
    if (state->stopEvent) { cudaEventDestroy(state->stopEvent); state->stopEvent = nullptr; }
    if (state->d_agents) { CUDA_CHECK(cudaFree(state->d_agents)); state->d_agents = nullptr; }
    if (state->h_agents) { delete[] state->h_agents; state->h_agents = nullptr; }
    if (state->d_obstacles) { 
        CUDA_CHECK(cudaFree(state->d_obstacles)); 
        CUDA_CHECK(cudaFreeHost(state->h_pinned_obstacles));
        state->d_obstacles = nullptr; 
        state->h_pinned_obstacles = nullptr;
        state->obstaclesCap = 0; 
    }
    destroySpatialHash(state->sh);
    delete state;
    state = nullptr;
}
