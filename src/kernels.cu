#include "kernels.h"
#include "agent.h"
#include "neighbor_query.cuh"
#include "spatial_hash.h"
#include <math.h>
#include <cmath>

// ─── GPU obstacle avoidance (device) ─────────────────────────────────────────
__device__ void gpuObstacleAvoidance(
    float px, float py, float vx, float vy,
    GPUObstacle* obs, int nObs,
    float& ax, float& ay)
{
    if (nObs == 0 || obs == nullptr) return;

    const float LOOK_AHEAD  = 0.15f;
    const float SAFETY_DIST = 0.08f;
    const float AVOID_W     = 3.0f;

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

// ─── Main boids kernel ────────────────────────────────────────────────────────
__global__ void boidsKernel(
    Agent* agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex,
    int tableSize, float cellSize,
    float2* renderPositions,

    float separation, float alignment, float cohesion,
    float perceptionRadius, float maxSpeed, float maxForce,
    float predatorRatio, float predatorSpeedMul, float fearWeight,
    float windX, float windY,
    bool attractorActive,
    float attractorX, float attractorY,
    float attractorStrength, float attractorRadius,
    float speedFactor,
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
        agents, particleIndex, cellStart, cellEnd,
        tableSize, perceptionRadius,
        &sepX, &sepY, &aliX, &aliY, &cohX, &cohY, &neighbours);

    if (neighbours > 0) {
        aliX /= neighbours; aliY /= neighbours;
        cohX = (cohX / neighbours) - self.x;
        cohY = (cohY / neighbours) - self.y;
    }

    float ax = sepX * (4.0f * separation)
             + aliX * (2.0f * alignment)
             + cohX * (1.5f * cohesion);
    float ay = sepY * (4.0f * separation)
             + aliY * (2.0f * alignment)
             + cohY * (1.5f * cohesion);

    // ── Real GPU obstacle avoidance ───────────────────────────────────────────
    gpuObstacleAvoidance(self.x, self.y, self.vx, self.vy,
                         d_obstacles, obstacleCount, ax, ay);

    // ── Wind ──────────────────────────────────────────────────────────────────
    ax += windX * 0.3f;
    ay += windY * 0.3f;

    // ── Attractor / repulsor ──────────────────────────────────────────────────
    if (attractorActive) {
        float dxA = attractorX - self.x;
        float dyA = attractorY - self.y;
        float distA = sqrtf(dxA*dxA + dyA*dyA);
        if (distA < attractorRadius && distA > 0.001f) {
            float force = attractorStrength * (1.0f - distA / attractorRadius);
            ax += (dxA / distA) * force;
            ay += (dyA / distA) * force;
        }
    }

    // ── Mouse repulsion ───────────────────────────────────────────────────────
    float dxM = mouseX - self.x, dyM = mouseY - self.y;
    float distM = sqrtf(dxM*dxM + dyM*dyM);
    if (distM < 0.5f && distM > 0.001f) {
        float strength = (0.5f - distM) / 0.5f;
        ax -= dxM * strength * 1.2f;
        ay -= dyM * strength * 1.2f;
    }

    // ── Boundary steering ─────────────────────────────────────────────────────
    const float margin = 0.9f, turnFactor = 0.5f;
    if (self.x >  margin) ax -= turnFactor;
    if (self.x < -margin) ax += turnFactor;
    if (self.y >  margin) ay -= turnFactor;
    if (self.y < -margin) ay += turnFactor;

    // ── Clamp force ───────────────────────────────────────────────────────────
    float forceMag = sqrtf(ax*ax + ay*ay);
    if (forceMag > maxForce && forceMag > 0.0001f) {
        ax = (ax / forceMag) * maxForce;
        ay = (ay / forceMag) * maxForce;
    }

    // ── Predator / prey behaviour ─────────────────────────────────────────────
    float currentMaxSpeed = maxSpeed;
    if (self.type == PREDATOR) {
        currentMaxSpeed = maxSpeed * predatorSpeedMul;
        ax *= predatorSpeedMul;
        ay *= predatorSpeedMul;
    }
    if (self.type == PREY) {
        ax -= fearWeight * sepX * 3.0f;
        ay -= fearWeight * sepY * 3.0f;
    }

    // ── Integrate ────────────────────────────────────────────────────────────
    if (isnan(ax) || isnan(ay)) { ax = 0.0f; ay = 0.0f; }

    self.vx += ax * dt * speedFactor;
    self.vy += ay * dt * speedFactor;
    self.vx *= 0.90f;
    self.vy *= 0.90f;

    float vel = sqrtf(self.vx*self.vx + self.vy*self.vy);
    if (vel > currentMaxSpeed) {
        self.vx = (self.vx / vel) * currentMaxSpeed;
        self.vy = (self.vy / vel) * currentMaxSpeed;
    }

    self.x += self.vx;
    self.y += self.vy;

    agents[i] = self;

    if (renderPositions != nullptr)
        renderPositions[i] = make_float2(self.x, self.y);
}

// ─── Launcher ────────────────────────────────────────────────────────────────
void launchBoidsKernel(
    Agent* d_agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex,
    int tableSize, float cellSize,
    float2* renderPositions,
    float separation, float alignment, float cohesion,
    float perceptionRadius, float maxSpeed, float maxForce,
    float predatorRatio, float predatorSpeedMul, float fearWeight,
    float windX, float windY,
    bool attractorActive,
    float attractorX, float attractorY,
    float attractorStrength, float attractorRadius,
    float speedFactor,
    GPUObstacle* d_obstacles, int obstacleCount
) {
    int blockSize = 256;
    int gridSize  = (count + blockSize - 1) / blockSize;

    boidsKernel<<<gridSize, blockSize>>>(
        d_agents, count, dt, mouseX, mouseY,
        cellStart, cellEnd, particleIndex,
        tableSize, cellSize, renderPositions,
        separation, alignment, cohesion,
        perceptionRadius, maxSpeed, maxForce,
        predatorRatio, predatorSpeedMul, fearWeight,
        windX, windY,
        attractorActive, attractorX, attractorY,
        attractorStrength, attractorRadius,
        speedFactor,
        d_obstacles, obstacleCount
    );
}
