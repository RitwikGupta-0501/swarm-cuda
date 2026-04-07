#include "spatial_hash.h"
#include <cuda_runtime.h>
#include <thrust/sort.h>
#include <thrust/device_ptr.h>

__global__ void assignCellsKernel(Agent* agents, int* agent_cells,
                                   int* agent_ids, int n, float cell_size,
                                   int table_size) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    int cx = (int)floorf(agents[i].x / cell_size);
    int cy = (int)floorf(agents[i].y / cell_size);
    agent_cells[i] = (unsigned int)((cx * 1610612741) ^ (cy * 805306457)) % table_size;
    agent_ids[i] = i;
}

__global__ void findBoundariesKernel(int* sorted_cells, int* cell_start,
                                      int* cell_end, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    int cell = sorted_cells[i];
    if (i == 0 || sorted_cells[i-1] != cell)
        cell_start[cell] = i;
    if (i == n-1 || sorted_cells[i+1] != cell)
        cell_end[cell] = i + 1;
}

void initSpatialHash(SpatialHash& sh, int max_agents) {
    sh.table_size = max_agents * 2;  // ~2x agent count
    cudaMalloc(&sh.agent_cells,   max_agents * sizeof(int));
    cudaMalloc(&sh.sorted_agents, max_agents * sizeof(int));
    cudaMalloc(&sh.cell_start,    sh.table_size * sizeof(int));
    cudaMalloc(&sh.cell_end,      sh.table_size * sizeof(int));
}

void destroySpatialHash(SpatialHash& sh) {
    cudaFree(sh.agent_cells);
    cudaFree(sh.sorted_agents);
    cudaFree(sh.cell_start);
    cudaFree(sh.cell_end);
}

void buildSpatialHash(SpatialHash& sh, Agent* d_agents, int count, float cell_size) {
    sh.agent_count = count;
    sh.cell_size = cell_size;

    int block = 256;
    int grid  = (count + block - 1) / block;

    // Step 1: assign cells
    assignCellsKernel<<<grid, block>>>(d_agents, sh.agent_cells,
                                       sh.sorted_agents, count,
                                       cell_size, sh.table_size);

    // Step 2: sort agents by cell (Thrust handles this on GPU)
    thrust::device_ptr<int> keys(sh.agent_cells);
    thrust::device_ptr<int> vals(sh.sorted_agents);
    thrust::sort_by_key(keys, keys + count, vals);

    // Step 3: reset cell_start sentinels
    cudaMemset(sh.cell_start, -1, sh.table_size * sizeof(int));
    cudaMemset(sh.cell_end,    0, sh.table_size * sizeof(int));

    // Step 4: find boundaries
    findBoundariesKernel<<<grid, block>>>(sh.agent_cells, sh.cell_start,
                                          sh.cell_end, count);
}
