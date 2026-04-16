# Architecture — Swarm Simulation

---

## Module Responsibilities

| Module | Owner | Language | Responsibility |
|--------|-------|----------|----------------|
| `main.cpp` | Shared | C++ | Entry point, render loop, GLFW/ImGui bootstrap |
| `simulation.cu/h` | Person 2 | CUDA/C++ | GPU agent state, CUDA–OpenGL interop |
| `kernels.cu/h` | Person 2 | CUDA | Boids physics kernel launcher |
| `spatial_hash.cu/h` | Person 3 | CUDA/Thrust | Hash-grid construction each frame |
| `neighbor_query.cuh` | Person 3 | CUDA device | Inline neighbour iteration |
| `agent.h` | Shared | C++ | `Agent` struct and `AgentType` enum |
| `ui.cpp/h` | Person 4 | C++ | ImGui panels, `SimParams`, `RenderOptions` |
| `presets.cpp/h` | Person 4 | C++ | Preset load/save with JSON I/O |
| `obstacles.cpp/h` | Person 4 | C++ | Obstacle structs, avoidance force, persistence |
| `scenarios.cpp/h` | Person 4 | C++ | Scripted demo scenarios |
| `renderer.cpp/h` | Person 1 | C++/OpenGL | Instanced draw, shaders, camera |
| `camera.cpp/h` | Person 1 | C++ | 2D ortho + 2.5D perspective |

---

## Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                          main loop                              │
│                                                                 │
│  glfwPollEvents()                                               │
│       │                                                         │
│       ▼                                                         │
│  renderFullUI()  ──► SimParams / RenderOptions / Obstacles      │
│       │                          │                              │
│       │                          ▼                              │
│       │            stepSimulation(dt, params)                   │
│       │                 │                                       │
│       │         ┌───────┴──────────────────────┐               │
│       │         │  GPU (CUDA)                  │               │
│       │         │  buildSpatialHash()           │               │
│       │         │  boidsKernel<<<>>>()          │               │
│       │         │    queryNeighbors()  (device) │               │
│       │         │  write → VBO (interop)        │               │
│       │         └───────────────────────────────┘               │
│       │                                                         │
│       ▼                                                         │
│  render(agentCount)   reads VBO ──► glDrawArraysInstanced()     │
│                                                                 │
│  ImGui::Render()                                                │
│  glfwSwapBuffers()                                              │
└─────────────────────────────────────────────────────────────────┘
```

---

## Key Data Structures

### `Agent` (agent.h — shared)
```cpp
struct Agent {
    float x, y;               // world position [-1, 1]
    float vx, vy;             // velocity
    float ax, ay;             // acceleration (scratch)
    float max_speed;
    float perception_radius;
    int   type;               // PREY=0, PREDATOR=1
};
```

### `SimParams` (ui.h — Person 4)
Carries all tunable floats from UI → `stepSimulation()`.  
Set `reinitRequested = true` to trigger re-spawn on next frame.

### `SpatialHash` (spatial_hash.h — Person 3)
Four GPU arrays built every frame via Thrust sort:
- `agent_cells[]` — hash bucket per agent
- `sorted_agents[]` — agent IDs sorted by bucket
- `cell_start[] / cell_end[]` — bucket boundaries in sorted list

### `Obstacle` (ui.h — Person 4)
CPU-side struct; avoidance force computed in `obstacles.cpp`.  
Future: upload to GPU constant memory for kernel-side avoidance.

---

## Integration Contracts

### Person 1 ↔ Person 4
- `RenderOptions` struct passed into `render()`.
- `colorScheme`, `agentSize`, `trailLength`, `showGrid`, `showVelocity` are read each frame.

### Person 2 ↔ Person 4
- `stepSimulation(dt, mouseX, mouseY, separation, alignment, cohesion, speed)` — extended signature to accept full `SimParams`.
- `initSimulation(agentCount)` called when `params.reinitRequested == true`.

### Person 3 ↔ Person 2
- `buildSpatialHash(sh, d_agents, count, cell_size)` called once per frame before kernel launch.
- `queryNeighbors(...)` is a `__device__` inline in `neighbor_query.cuh`.

### Person 4 ↔ Everyone
- Person 4 **reads** stats (FPS, sim time) and fills `SimStats`.
- Person 4 **writes** `SimParams` and `RenderOptions`; others read them.
- `Obstacle` list is CPU-only; Person 2 will eventually mirror it to GPU.

---

## Threading & Synchronisation

All CUDA work is submitted to the **default stream**.  
`cudaDeviceSynchronize()` is called after the boids kernel to ensure the VBO is complete before OpenGL draws.  
ImGui runs entirely on the CPU thread.

---

## Build System (CMakeLists.txt)

```
project(swarm_sim LANGUAGES CXX CUDA)

add_executable(swarm_sim
    src/main.cpp
    src/simulation.cu
    src/kernels.cu
    src/spatial_hash.cu
    src/ui.cpp          # Person 4
    src/presets.cpp     # Person 4
    src/obstacles.cpp   # Person 4
    src/scenarios.cpp   # Person 4
    src/renderer.cpp    # Person 1
    src/camera.cpp      # Person 1
)
```
