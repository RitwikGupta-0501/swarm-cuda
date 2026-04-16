#include "kernels.h"
#include "agent.h"
#include <math.h>
#include "neighbor_query.cuh"

// GPU kernel: one thread per agent
__global__ void boidsKernel(
    Agent* agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex,
    int tableSize, float cellSize,
    RenderAgent* renderPositions
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;

    Agent self = agents[i];

    float sepX = 0.0f, sepY = 0.0f;
    float aliX = 0.0f, aliY = 0.0f;
    float cohX = 0.0f, cohY = 0.0f;

    int neighbours = 0;

    // neighbor loop
    queryNeighbors(
        i,                           // agent_idx
        self.x, self.y,              // px, py
        cellSize,                    // cell_size
        agents,                      // agents buffer
        particleIndex,               // sorted_agents
        cellStart,                   // cell_start
        cellEnd,                     // cell_end
        tableSize,      // table_size (total number of cells)
        self.perception_radius,      // radius
        &sepX, &sepY,                // pointers to Separation accumulators
        &aliX, &aliY,                // pointers to Alignment accumulators
        &cohX, &cohY,                // pointers to Cohesion accumulators
        &neighbours                  // pointer to neighbor count
    );

    // averages
    if (neighbours > 0) {
        aliX /= neighbours;
        aliY /= neighbours;

        cohX = (cohX / neighbours) - self.x;
        cohY = (cohY / neighbours) - self.y;
    }

    // tuned weights
    float w_sep = 1.0f;
    float w_ali = 0.5f;
    float w_coh = 0.2f;

    float ax = sepX * w_sep + aliX * w_ali + cohX * w_coh;
    float ay = sepY * w_sep + aliY * w_ali + cohY * w_coh;

    // mouse repulsion with distance falloff
    float dxMouse = mouseX - self.x;
    float dyMouse = mouseY - self.y;
    float distMouse = sqrtf(dxMouse * dxMouse + dyMouse * dyMouse);

    if (distMouse < 0.5f && distMouse > 0.001f) {
        float strength = (0.5f - distMouse) / 0.5f;
        ax -= dxMouse * strength * 1.2f;
        ay -= dyMouse * strength * 1.2f;
    }

    // boundary steering
    float margin = 0.9f;
    float turnFactor = 0.5f;

    if (self.x > margin)  ax -= turnFactor;
    if (self.x < -margin) ax += turnFactor;

    if (self.y > margin)  ay -= turnFactor;
    if (self.y < -margin) ay += turnFactor;

    // limit acceleration
    float maxForce = 0.1f;
    float forceMag = sqrtf(ax * ax + ay * ay);
    if (forceMag > maxForce) {
        ax = (ax / forceMag) * maxForce;
        ay = (ay / forceMag) * maxForce;
    }

    // update velocity
    self.vx += ax * dt;
    self.vy += ay * dt;

    // damping
    self.vx *= 0.90f;
    self.vy *= 0.90f;

    // limit speed
    float speed = sqrtf(self.vx * self.vx + self.vy * self.vy);
    if (speed > self.max_speed) {
        self.vx = (self.vx / speed) * self.max_speed;
        self.vy = (self.vy / speed) * self.max_speed;
    }

    // update position
    self.x += self.vx * dt;
    self.y += self.vy * dt;

    agents[i] = self;

    if (renderPositions != nullptr) {
        renderPositions[i].px = self.x;
        renderPositions[i].py = self.y;
        renderPositions[i].vx = self.vx;
        renderPositions[i].vy = self.vy;
    }
}

// kernel launcher
void launchBoidsKernel(
    Agent* d_agents, int count, float dt, float mouseX, float mouseY,
    int* cellStart, int* cellEnd, int* particleIndex,
    int tableSize, float cellSize,
    RenderAgent* renderPositions
) {
    int blockSize = 256;
    int gridSize = (count + blockSize - 1) / blockSize;

    boidsKernel<<<gridSize, blockSize>>>(
        d_agents, count, dt, mouseX, mouseY,
        cellStart, cellEnd, particleIndex,
        tableSize, cellSize,
        renderPositions
    );
}
