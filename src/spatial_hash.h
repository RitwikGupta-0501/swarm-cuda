#pragma once
#include "agent.h"

struct SpatialHash {
    int*   agent_cells;    // cell hash each agent belongs to
    int*   sorted_agents;  // agent IDs sorted by cell
    int*   cell_start;     // where each cell begins in sorted list
    int*   cell_end;       // where each cell ends
    int    table_size;     // number of buckets
    float  cell_size;      // = perception_radius
    int    agent_count;
};

// Call this every frame before stepSimulation
void buildSpatialHash(SpatialHash& sh, Agent* d_agents, int count, float cell_size);

// Allocate/free GPU memory for the hash
void initSpatialHash(SpatialHash& sh, int max_agents);
void destroySpatialHash(SpatialHash& sh);
