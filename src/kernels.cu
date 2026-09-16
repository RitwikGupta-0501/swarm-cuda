#include "kernels.h"
#include "agent.h"
#include "neighbor_query.cuh"
#include <stdio.h>
#include <math.h>
#include <cmath>

__constant__ SimConfig d_config;

void uploadSimConfig(const SimConfig& config) {
    cudaMemcpyToSymbol(d_config, &config, sizeof(SimConfig));
}

// ─── GPU obstacle avoidance (device) ─────────────────────────────────────────
__device__ void gpuObstacleAvoidance(
    float px, float py, float vx, float vy,
    GPUObstacle* obs, int nObs,
    float& ax, float& ay)
{
    if (nObs == 0 || obs == nullptr) return;

    const float LOOK_AHEAD  = d_config.lookAhead;
    const float SAFETY_DIST = d_config.safetyDist;
    const float AVOID_W     = d_config.avoidWeight;

    float speed = sqrtf(vx*vx + vy*vy);
    float nx = (speed > 0.0001f) ? vx/speed : 0.0f;
    float ny = (speed > 0.0001f) ? vy/speed : 0.0f;

    float aheadX = px + nx * LOOK_AHEAD;
    float aheadY = py + ny * LOOK_AHEAD;

    for (int k = 0; k < nObs; k++) {
        GPUObstacle& o = obs[k];
        float steerX = 0.0f, steerY = 0.0f;
        bool  hit    = false;

        if (o.type == 0) {                       // circle
            float dx = aheadX - o.x;
            float dy = aheadY - o.y;
            float d  = sqrtf(dx*dx + dy*dy);
            if (d < o.radius + SAFETY_DIST) {
                float inv = (d > 0.0001f) ? 1.0f/d : 0.0f;
                steerX = dx*inv; steerY = dy*inv; hit = true;
            }
        } else if (o.type == 1) {               // rect (AABB)
            float dx   = aheadX - o.x;
            float dy   = aheadY - o.y;
            float overX = o.x2 + SAFETY_DIST - fabsf(dx);
            float overY = o.y2 + SAFETY_DIST - fabsf(dy);
            if (overX > 0.0f && overY > 0.0f) {
                if (overX < overY) steerX = (dx > 0.0f) ?  1.0f : -1.0f;
                else               steerY = (dy > 0.0f) ?  1.0f : -1.0f;
                hit = true;
            }
        } else if (o.type == 2) {               // line segment
            float ex = o.x2 - o.x, ey = o.y2 - o.y;
            float len = sqrtf(ex*ex + ey*ey);
            if (len < 0.0001f) continue;
            float t = ((aheadX-o.x)*ex + (aheadY-o.y)*ey) / (len*len);
            t = fmaxf(0.0f, fminf(1.0f, t));
            float cx = o.x + t*ex, cy = o.y + t*ey;
            float dx = aheadX - cx, dy = aheadY - cy;
            float d  = sqrtf(dx*dx + dy*dy);
            if (d < SAFETY_DIST) {
                float inv = (d > 0.0001f) ? 1.0f/d : 0.0f;
                steerX = dx*inv; steerY = dy*inv; hit = true;
            }
        }

        if (hit) { ax += steerX * AVOID_W; ay += steerY * AVOID_W; }
    }
}

__global__ void boidsKernel(
    Agent* agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex, const Agent* sorted_agents_data,
    int tableSize, float cellSize,
    float4* renderPositions,
    GPUObstacle* d_obstacles, int obstacleCount
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;

    Agent self = agents[i];
    self.max_speed = maxSpeed;

    float sepX = 0, sepY = 0;
    float aliX = 0, aliY = 0;
    float cohX = 0, cohY = 0;
    int   neighbours = 0;

    queryNeighbors(
        i, self.x, self.y, cellSize,
        sorted_agents_data, particleIndex, cellStart, cellEnd,
        tableSize, d_config.perceptionRadius,
        &sepX, &sepY, &aliX, &aliY, &cohX, &cohY, &neighbours);

    if (neighbours > 0) {
        aliX /= neighbours; aliY /= neighbours;
        cohX = (cohX / neighbours) - self.x;
        cohY = (cohY / neighbours) - self.y;
    }

    float ax = sepX * (d_config.sepWeight * d_config.separation)
             + aliX * (d_config.aliWeight * d_config.alignment)
             + cohX * (d_config.cohWeight * d_config.cohesion);
    float ay = sepY * (d_config.sepWeight * d_config.separation)
             + aliY * (d_config.aliWeight * d_config.alignment)
             + cohY * (d_config.cohWeight * d_config.cohesion);

    // ── Real GPU obstacle avoidance ───────────────────────────────────────────
    gpuObstacleAvoidance(self.x, self.y, self.vx, self.vy,
                         d_obstacles, obstacleCount, ax, ay);

    // ── Wind ──────────────────────────────────────────────────────────────────
    ax += d_config.windX * 0.3f;
    ay += d_config.windY * 0.3f;

    // ── Attractor / repulsor ──────────────────────────────────────────────────
    if (d_config.attractorActive) {
        float dxA = d_config.attractorX - self.x;
        float dyA = d_config.attractorY - self.y;
        float distA = sqrtf(dxA*dxA + dyA*dyA);
        if (distA < d_config.attractorRadius && distA > 0.001f) {
            float force = d_config.attractorStrength * (1.0f - distA / d_config.attractorRadius);
            ax += (dxA / distA) * force;
            ay += (dyA / distA) * force;
        }
    }

    // ── Mouse repulsion ───────────────────────────────────────────────────────
    float dxM = mouseX - self.x, dyM = mouseY - self.y;
    float distM = sqrtf(dxM*dxM + dyM*dyM);
    if (distM < 0.5f && distM > 0.001f) {
        float strength = (0.5f - distM) / 0.5f;
        ax -= dxM * strength * 1.2f; // Could be config too, but we'll leave it for now
        ay -= dyM * strength * 1.2f;
    }

    // ── Boundary steering ─────────────────────────────────────────────────────
    const float margin = d_config.boundaryMargin;
    const float turnFactor = d_config.boundaryTurnFactor;
    if (self.x >  margin) ax -= turnFactor;
    if (self.x < -margin) ax += turnFactor;
    if (self.y >  margin) ay -= turnFactor;
    if (self.y < -margin) ay += turnFactor;

    // ── Clamp force ───────────────────────────────────────────────────────────
    float forceMag = sqrtf(ax*ax + ay*ay);
    if (forceMag > d_config.maxForce && forceMag > 0.0001f) {
        ax = (ax / forceMag) * d_config.maxForce;
        ay = (ay / forceMag) * d_config.maxForce;
    }

    // ── Predator / prey behaviour ─────────────────────────────────────────────
    float currentMaxSpeed = d_config.maxSpeed;
    if (self.type == PREDATOR) {
        currentMaxSpeed = d_config.maxSpeed * d_config.predatorSpeedMul;
        ax *= d_config.predatorSpeedMul;
        ay *= d_config.predatorSpeedMul;
    }
    if (self.type == PREY) {
        ax -= d_config.fearWeight * sepX * 3.0f; // 3.0f can be fearRepelWeight if added
        ay -= d_config.fearWeight * sepY * 3.0f;
    }

    // ── Integrate ────────────────────────────────────────────────────────────
    if (isnan(ax) || isnan(ay)) { ax = 0.0f; ay = 0.0f; }

    self.vx += ax * dt * d_config.speedFactor;
    self.vy += ay * dt * d_config.speedFactor;
    self.vx *= 0.99f;
    self.vy *= 0.99f;

    float vel = sqrtf(self.vx*self.vx + self.vy*self.vy);
    if (vel > currentMaxSpeed) {
        self.vx = (self.vx / vel) * currentMaxSpeed;
        self.vy = (self.vy / vel) * currentMaxSpeed;
    }

    self.x += self.vx;
    self.y += self.vy;

    agents[i] = self;

    if (renderPositions != nullptr) {
        renderPositions[i] = make_float4(self.x, self.y, self.vx, self.vy);
    }
}

// ─── Launcher ────────────────────────────────────────────────────────────────
void launchBoidsKernel(
    Agent* d_agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex, const Agent* sorted_agents_data,
    int tableSize, float cellSize,
    float4* renderPositions,
    GPUObstacle* d_obstacles, int obstacleCount
) {
    int blockSize = 256;
    int gridSize  = (count + blockSize - 1) / blockSize;

    boidsKernel<<<gridSize, blockSize>>>(
        d_agents, count, dt, mouseX, mouseY,
        cellStart, cellEnd, particleIndex, sorted_agents_data,
        tableSize, cellSize, renderPositions,
        d_obstacles, obstacleCount
    );
}
