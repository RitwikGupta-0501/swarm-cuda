# Swarm Intelligence Visualizer (Rendering + CUDA Interop)

## Build (Windows, CMake + vcpkg)

1. Install vcpkg and integrate with CMake (manifest mode).
2. Configure:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="<vcpkg>/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

## CUDA ↔ OpenGL interop contract

- The renderer creates a shared OpenGL VBO sized for `max_agents * sizeof(Agent)` and registers it with CUDA.
- The simulation maps/unmaps the resource each frame and writes directly to the GPU buffer.
- The renderer reads it as instanced vertex attributes (no CPU copies).

### Exact `Agent` memory layout

```cpp
// Offsets (bytes): position=(0..7), velocity=(8..15). Total stride: 16 bytes.
struct Agent {
  float position_x;
  float position_y;
  float velocity_x;
  float velocity_y;
};
static_assert(sizeof(Agent) == 16);
```

## Runtime Controls

| Key | Action |
|---|---|
| `TAB` | Toggle 2D Ortho ↔ 2.5D Perspective camera |
| `1`–`5` | Visualization modes: Uniform / VelocityHeat / Direction / RainbowTime / TypeBased |
| `WASD` | Pan camera (2D) |
| `Q / E` | Rotate camera (2D) |
| `Scroll` | Zoom (zoom-to-cursor in 2D) |
| `RMB drag` | Pan (2D) / Orbit (2.5D) |
| **`V`** | **Toggle velocity-vector debug overlay** |
| **`T`** | **Toggle motion trails** |
| **`C`** | **Toggle GPU frustum culling** |
| **`G`** | **Toggle additive glow / bloom post-process** |
| `ESC` | Quit |

## Rendering Features (Person 1)

| Feature | Status | Notes |
|---|---|---|
| OpenGL 4.6 context + GLFW | ✅ Done | |
| 2D Orthographic camera + pan/zoom/rotate | ✅ Done | Exponential smoothing |
| 2.5D Perspective orbit camera | ✅ Done | Smooth yaw/pitch/distance |
| Agent instanced rendering (triangles) | ✅ Done | Arrow shape, velocity-aligned |
| LOD: GL_POINTS at low zoom | ✅ Done | Auto-switches at zoom < 0.2 |
| Visualization modes (4 modes) | ✅ Done | Heat / Direction / Rainbow / Uniform |
| Debug grid & axes overlay | ✅ Done | World-space, adaptive step |
| Debug overlay text (FPS, agents, mode) | ✅ Done | Built-in bitmap font |
| **2.5D Billboarding** | ✅ Done | Agents face camera, heading preserved |
| **Debug velocity vectors** | ✅ Done | Toggle with `V`; GPU-driven via SSBO |
| **GPU frustum culling** | ✅ Done | Toggle with `C`; compute + indirect draw |
| **Motion trails (ring buffer)** | ✅ Done | Toggle with `T`; 20-slot SSBO ring buffer |
