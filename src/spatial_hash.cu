#include "spatial_hash.h"
#include <cuda_runtime.h>
#include <thrust/sort.h>
#include <thrust/device_ptr.h>
#include <cstdio>

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

__global__ void reorderAgentsKernel(Agent* agents, Agent* sorted_agents_data, int* sorted_agents, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    sorted_agents_data[i] = agents[sorted_agents[i]];
}

void initSpatialHash(SpatialHash& sh, int max_agents) {
    sh.table_size = max_agents * 2;  // ~2x agent count
    if (sh.table_size < 1024) sh.table_size = 1024;
    CUDA_CHECK(cudaMalloc(&sh.agent_cells,   max_agents * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&sh.sorted_agents, max_agents * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&sh.cell_start,    sh.table_size * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&sh.cell_end,      sh.table_size * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&sh.sorted_agents_data, max_agents * sizeof(Agent)));
}

void destroySpatialHash(SpatialHash& sh) {
    CUDA_CHECK(cudaFree(sh.agent_cells));
    CUDA_CHECK(cudaFree(sh.sorted_agents));
    CUDA_CHECK(cudaFree(sh.cell_start));
    CUDA_CHECK(cudaFree(sh.cell_end));
    CUDA_CHECK(cudaFree(sh.sorted_agents_data));
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
    CUDA_CHECK(cudaMemset(sh.cell_start, -1, sh.table_size * sizeof(int)));
    CUDA_CHECK(cudaMemset(sh.cell_end,    0, sh.table_size * sizeof(int)));

    // Step 4: find boundaries
    findBoundariesKernel<<<grid, block>>>(sh.agent_cells, sh.cell_start,
                                          sh.cell_end, count);

    // Step 5: reorder actual agent data
    reorderAgentsKernel<<<grid, block>>>(d_agents, sh.sorted_agents_data, sh.sorted_agents, count);
}
