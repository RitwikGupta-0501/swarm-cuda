#pragma once
#include "agent.h"

struct SpatialHash {
    int* cell_start;
    int* cell_end;
    int* sorted_agents;

    int* agent_cells;   // required
    int  agent_count;   // required

    int table_size;
    float cell_size;
};

// function declarations
void initSpatialHash(SpatialHash& sh, int max_agents);
void buildSpatialHash(SpatialHash& sh, Agent* d_agents, int count, float cell_size);
void destroySpatialHash(SpatialHash& sh);