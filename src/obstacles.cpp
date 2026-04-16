#include "obstacles.h"
#include <cmath>
#include <fstream>
#include <cstdio>

// ─── Avoidance force helper ───────────────────────────────────────────────────
// Ray-cast look-ahead: sample a point "ahead" of the agent and steer away
// from any obstacle boundary that is within a safety distance.

static const float LOOK_AHEAD   = 0.15f;   // how far ahead to probe
static const float SAFETY_DIST  = 0.08f;   // clearance from obstacle surface

void computeObstacleAvoidance(
    float px, float py,
    float vx, float vy,
    const std::vector<Obstacle>& obstacles,
    float avoidWeight,
    float& outAx, float& outAy)
{
    outAx = 0.0f; outAy = 0.0f;
    if (obstacles.empty() || avoidWeight <= 0.0f) return;

    // normalise velocity for look-ahead
    float speed = std::sqrtf(vx*vx + vy*vy);
    float nx = (speed > 0.0001f) ? vx/speed : 0.0f;
    float ny = (speed > 0.0001f) ? vy/speed : 0.0f;

    float aheadX = px + nx * LOOK_AHEAD;
    float aheadY = py + ny * LOOK_AHEAD;

    for (const Obstacle& o : obstacles) {
        float steerX = 0.0f, steerY = 0.0f;
        bool  hit    = false;

        if (o.type == OBS_CIRCLE) {
            float dx = aheadX - o.x;
            float dy = aheadY - o.y;
            float d  = std::sqrtf(dx*dx + dy*dy);
            if (d < o.radius + SAFETY_DIST) {
                // push away from centre
                float inv = (d > 0.0001f) ? 1.0f/d : 0.0f;
                steerX = dx * inv;
                steerY = dy * inv;
                hit = true;
            }
        } else if (o.type == OBS_RECT) {
            // AABB: o.(x,y) = centre, o.(x2,y2) = half-extents
            float dx = aheadX - o.x;
            float dy = aheadY - o.y;
            float overX = o.x2 + SAFETY_DIST - std::fabsf(dx);
            float overY = o.y2 + SAFETY_DIST - std::fabsf(dy);
            if (overX > 0.0f && overY > 0.0f) {
                // push along axis of smallest penetration
                if (overX < overY) steerX = (dx > 0.0f) ?  1.0f : -1.0f;
                else               steerY = (dy > 0.0f) ?  1.0f : -1.0f;
                hit = true;
            }
        } else if (o.type == OBS_LINE) {
            // Segment o.(x,y) → o.(x2,y2): point-to-line distance
            float ex = o.x2 - o.x, ey = o.y2 - o.y;
            float len = std::sqrtf(ex*ex + ey*ey);
            if (len < 0.0001f) continue;
            float t = ((aheadX-o.x)*ex + (aheadY-o.y)*ey) / (len*len);
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            float cx = o.x + t*ex, cy = o.y + t*ey;
            float dx = aheadX - cx, dy = aheadY - cy;
            float d  = std::sqrtf(dx*dx + dy*dy);
            if (d < SAFETY_DIST) {
                float inv = (d > 0.0001f) ? 1.0f/d : 0.0f;
                steerX = dx * inv;
                steerY = dy * inv;
                hit = true;
            }
        }

        if (hit) {
            outAx += steerX * avoidWeight;
            outAy += steerY * avoidWeight;
        }
    }
}

// ─── Moving obstacles ─────────────────────────────────────────────────────────
void updateMovingObstacles(std::vector<Obstacle>& obstacles, float dt) {
    for (Obstacle& o : obstacles) {
        if (!o.isMoving) continue;
        o.x += o.moveSpeedX * dt;
        o.y += o.moveSpeedY * dt;
        // bounce off world boundary [-1, 1]
        if (o.x >  0.95f) { o.x =  0.95f; o.moveSpeedX = -o.moveSpeedX; }
        if (o.x < -0.95f) { o.x = -0.95f; o.moveSpeedX = -o.moveSpeedX; }
        if (o.y >  0.95f) { o.y =  0.95f; o.moveSpeedY = -o.moveSpeedY; }
        if (o.y < -0.95f) { o.y = -0.95f; o.moveSpeedY = -o.moveSpeedY; }
    }
}

// ─── Persistence ─────────────────────────────────────────────────────────────
void saveObstacles(const std::vector<Obstacle>& obstacles, const char* path) {
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "[\n";
    for (size_t i = 0; i < obstacles.size(); i++) {
        const Obstacle& o = obstacles[i];
        f << "  { \"type\": " << (int)o.type
          << ", \"x\": "  << o.x  << ", \"y\": "  << o.y
          << ", \"x2\": " << o.x2 << ", \"y2\": " << o.y2
          << ", \"radius\": " << o.radius
          << ", \"moving\": " << (o.isMoving ? "true" : "false")
          << ", \"mvx\": " << o.moveSpeedX << ", \"mvy\": " << o.moveSpeedY
          << " }";
        if (i + 1 < obstacles.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
}

bool loadObstacles(std::vector<Obstacle>& obstacles, const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    obstacles.clear();
    // Minimal parser matching the format saved above
    std::string line;
    while (std::getline(f, line)) {
        if (line.find('{') == std::string::npos) continue;
        Obstacle o{};
        int typeInt = 0, movingInt = 0;
        // sscanf for our exact format
        std::sscanf(line.c_str(),
            "  { \"type\": %d, \"x\": %f, \"y\": %f, \"x2\": %f, \"y2\": %f,"
            " \"radius\": %f, \"moving\": %d, \"mvx\": %f, \"mvy\": %f }",
            &typeInt, &o.x, &o.y, &o.x2, &o.y2,
            &o.radius, &movingInt, &o.moveSpeedX, &o.moveSpeedY);
        o.type     = (ObstacleType)typeInt;
        o.isMoving = (movingInt != 0);
        obstacles.push_back(o);
    }
    return !obstacles.empty();
}
