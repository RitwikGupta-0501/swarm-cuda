#pragma once

enum AgentType {
    PREY = 0,
    PREDATOR = 1
};

struct Agent {
    float x, y;
    float vx, vy;
    float ax, ay;

    float max_speed;
    float perception_radius;

    int type; // predator or prey
};
