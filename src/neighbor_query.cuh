#pragma once
#include "agent.h"
#include "spatial_hash.h"

__device__ inline int hashCell(int cx, int cy, int table_size) {
    return (unsigned int)((cx * 1610612741) ^ (cy * 805306457)) % table_size;
}

__device__ inline void queryNeighbors(
    int           agent_idx,
    float         px, float py,
    float         cell_size,
    Agent*        agents,
    const int*    sorted_agents,
    const int*    cell_start,
    const int*    cell_end,
    int           table_size,
    float         radius,
    // boids accumulators passed by pointer:
    float* sepX, float* sepY,
    float* aliX, float* aliY,
    float* cohX, float* cohY,
    int*   neighbors
) {
    int cx = (int)floorf(px / cell_size);
    int cy = (int)floorf(py / cell_size);

    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            int cell = hashCell(cx + dx, cy + dy, table_size);
            int start = cell_start[cell];
            if (!__any_sync(0xFFFFFFFF, start != -1)) continue;
            int end = cell_end[cell];

            for (int j = start; j < end; j++) {
                int nid = sorted_agents[j];
                if (nid == agent_idx) continue;

                float ddx = agents[nid].x - px;
                float ddy = agents[nid].y - py;
                float dist = sqrtf(ddx*ddx + ddy*ddy) + 1e-6f;

                if (dist < radius && dist > 0.0f) {
                    *aliX += agents[nid].vx;
                    *aliY += agents[nid].vy;
                    *cohX += agents[nid].x;
                    *cohY += agents[nid].y;
                    (*neighbors)++;
                }
                if (dist < 0.02f && dist > 0.0f) {
                    *sepX -= ddx / dist;
                    *sepY -= ddy / dist;
                }
            }
        }
    }
}
