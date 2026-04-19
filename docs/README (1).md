# Swarm Simulation

A real-time GPU-accelerated boids / swarm simulation built with **CUDA**, **OpenGL**, and **ImGui**.  
Supports up to ~200 000 agents running at interactive frame rates using spatial-hash neighbour queries.

---

## What It Does

The simulation models emergent flocking behaviour using three classic steering rules:

| Rule | Description |
|------|-------------|
| **Separation** | Agents steer away from crowded neighbours |
| **Alignment** | Agents match the average heading of neighbours |
| **Cohesion** | Agents steer toward the average position of neighbours |

On top of the boids core the project adds:

- **Predator / Prey dynamics** — predators chase prey; prey panic and scatter
- **Obstacle avoidance** — circle, rectangle, and line obstacles with ray-cast steering
- **Environmental effects** — configurable wind and attractor / repulsor points
- **Scenario system** — five scripted demos (Murmuration, Predator Attack, Obstacle Course, Migration, …)
- **Preset system** — five built-in parameter sets with JSON save/load
- **Full ImGui UI** — sliders, dropdowns, live stats, export tools

---

## Build Instructions

### Prerequisites

| Tool | Version |
|------|---------|
| CUDA Toolkit | 11.x or 12.x |
| CMake | ≥ 3.18 |
| GLFW | 3.x |
| GLAD | OpenGL 3.3 core |
| ImGui | 1.89+ (docking branch optional) |
| Thrust | bundled with CUDA |

### Steps

```bash
git clone <repo-url>
cd swarm-simulation
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./swarm_sim
```

On Windows with MSVC:
```bash
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

---

## Controls

| Input | Action |
|-------|--------|
| **Mouse move** | Red cursor repels nearby agents |
| **Mouse drag** | Pan camera (when camera controls enabled) |
| **Scroll** | Zoom in / out |
| **F** | Toggle fullscreen |
| **Space** | Pause / resume |
| **R** | Reset all agents |

---

## Parameter Explanations

### Boids

| Parameter | Range | Effect |
|-----------|-------|--------|
| Separation | 0 – 5 | How strongly agents avoid crowding |
| Alignment | 0 – 5 | How strongly agents match neighbour heading |
| Cohesion | 0 – 5 | How tightly agents cluster together |
| Perception Radius | 0.05 – 0.5 | Neighbourhood search radius (world units) |
| Max Speed | 0.1 – 2.0 | Top velocity cap |
| Max Force | 0.01 – 0.5 | Steering force limit per frame |
| Time Scale | 0.1 – 5.0 | Simulation speed multiplier |

### Predator / Prey

| Parameter | Effect |
|-----------|--------|
| Predator Ratio | Fraction of agents spawned as predators |
| Predator Speed Mul | Speed multiplier relative to prey |
| Fear Weight | Extra separation force prey apply when near a predator |

---

## Project Structure

```
swarm-simulation/
├── src/
│   ├── main.cpp          # Entry point, render loop
│   ├── simulation.cu/h   # CUDA simulation step
│   ├── kernels.cu/h      # Boids GPU kernel
│   ├── spatial_hash.cu/h # Hash-grid neighbour queries
│   ├── neighbor_query.cuh# Device-side query function
│   ├── agent.h           # Shared Agent struct
│   ├── ui.cpp/h          # ImGui interface (Person 4)
│   ├── presets.cpp/h     # Preset load/save
│   ├── obstacles.cpp/h   # Obstacle avoidance
│   └── scenarios.cpp/h   # Scripted demo scenarios
├── shaders/
│   ├── agent.vert/frag   # Instanced agent rendering
│   └── trail.vert/frag   # Motion trail rendering
├── config/
│   └── presets.json      # Saved parameter presets
└── docs/
    ├── README.md
    ├── USER_MANUAL.md
    ├── ARCHITECTURE.md
    └── API.md
```
