#pragma once

#include <cstddef>
#include <cstdint>

// Exact shared memory layout for CUDA <-> OpenGL buffer.
// Do NOT change without coordinating with the CUDA simulation.
struct RenderAgent {
  float position_x; // offset 0
  float position_y; // offset 4
  float velocity_x; // offset 8
  float velocity_y; // offset 12
};

static_assert(sizeof(RenderAgent) == 16, "Agent stride must be 16 bytes.");
static_assert(offsetof(RenderAgent, position_x) == 0);
static_assert(offsetof(RenderAgent, position_y) == 4);
static_assert(offsetof(RenderAgent, velocity_x) == 8);
static_assert(offsetof(RenderAgent, velocity_y) == 12);

