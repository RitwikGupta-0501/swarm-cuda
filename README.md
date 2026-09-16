# Swarm-CUDA

A GPU-accelerated massive swarm simulation supporting up to 200,000+ interacting agents (boids) in real-time, written in C++, CUDA, and OpenGL.

![Swarm Simulation Preview](images/screenshot.png)

## Overview

Swarm-CUDA demonstrates real-time physics simulations on the GPU. It uses Craig Reynolds' Boids algorithm combined with a spatial hashing technique implemented in CUDA for O(N) neighbor lookups. The simulation avoids expensive host-device memory transfers by directly registering CUDA memory with OpenGL Vertex Buffer Objects (VBOs) for zero-copy rendering.

### Key Features
- **Massive scale:** Simulate >200,000 agents at 60 FPS.
- **Predator-Prey dynamics:** Complex emerging behaviors with varying predator ratios and fear parameters.
- **Interactive Scenarios:** Built-in scenarios like Murmuration, Predator Attack, Migration, and Obstacle Course.
- **Zero-Copy Rendering:** Direct CUDA to OpenGL interop for instanced rendering.
- **Moving Obstacles:** Avoidance logic running on the GPU for static and dynamic boundaries.
- **Rich UI:** Live control over simulation parameters (separation, alignment, cohesion, perception radius).

## Build Instructions

### Prerequisites
- **CUDA Toolkit** (11.0 or newer)
- **CMake** (3.18 or newer)
- **OpenGL 3.3+** compatible driver and hardware
- A C++17 compatible compiler (e.g., GCC, Clang, or MSVC)

### Building (Linux/macOS)
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Running
```bash
./swarm-sim
```

## Controls

| Input / Action | Description |
|---|---|
| **Left Click + Drag** | Pan the camera (2D mode) or orbit (2.5D mode). |
| **Scroll Wheel** | Zoom in / out. |
| **Right Click + Drag** | Adjust camera altitude (2.5D mode only). |
| **Ctrl + Left Click** | Teleport the attractor to the cursor location. |
| **Space** | Pause / Play simulation. |
| **R** | Reset simulation to defaults. |
| **F1** | Toggle UI visibility. |
| **F2** | Toggle full-screen mode. |

## Architecture

For a deeper dive into how the spatial hashing, CUDA-GL interop, and physics kernels work, please refer to our internal [ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Dependencies

Included as submodules or headers:
- `glad` (OpenGL loader)
- `glfw` (Window creation)
- `glm` (Math library)
- `imgui` (Immediate mode UI)
- `nlohmann/json` (State saving and loading)
