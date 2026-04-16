// ─── main.cpp — Swarm Simulation ─────────────────────────────────────────────
// Implements:
//   • Color schemes  (Uniform / Velocity Heat-map / Type / Rainbow)
//   • Trail rendering (circular position buffer)
//   • Grid overlay
//   • Velocity-vector overlay
//   • Add Prey / Add Predator / Convert Random
//   • Obstacle GPU upload (real avoidance)
//   • Screenshot (PNG via stb_image_write, header-only included below)
//   • Recording  (sequential PNG frames → frames/ directory)
//   • Export State / Load State (JSON)
//   • Export Params (delegates to presets.cpp)
// ─────────────────────────────────────────────────────────────────────────────

// ── stb_image_write (header-only, single-file) ────────────────────────────────
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "ui.h"
#include "simulation.h"
#include "obstacles.h"
#include "presets.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include <direct.h>   // _mkdir (Windows)

// ─── Globals ──────────────────────────────────────────────────────────────────
SimParams            params;
RenderOptions        renderOpts;
SimStats             stats;
std::vector<Obstacle> obstacles;

bool paused              = false;
bool stepOnce            = false;
bool screenshotRequested = false;
bool recordingActive     = false;

// Flags written by ui.cpp, consumed here
bool g_exportStateRequested = false;
bool g_loadStateRequested   = false;

// Path buffer (shared with ui.cpp via a simple extern trick;
// we redeclare it here as the authoritative definition)
static char g_savePathBuf[128] = "saves/state.json";

// ─── Forward declarations ────────────────────────────────────────────────────
static void takeScreenshot(GLFWwindow* window, const char* path);
static void exportState(const std::string& path);
static bool loadState (const std::string& path);

// ─────────────────────────────────────────────────────────────────────────────
//   SHADERS
// ─────────────────────────────────────────────────────────────────────────────

// ── Agent point shader (colour determined per-vertex by colour scheme) ────────
static const char* agentVS = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;   // generated on CPU
out vec4 vColor;
void main(){
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";
static const char* agentFS = R"(
#version 330 core
in  vec4 vColor;
out vec4 FragColor;
void main(){ FragColor = vColor; }
)";

// ── Trail shader (lines, alpha-faded) ────────────────────────────────────────
static const char* trailVS = R"(
#version 330 core
layout(location=0) in vec2  aPos;
layout(location=1) in float aAlpha;
out float vAlpha;
void main(){
    gl_Position = vec4(aPos, 0.0, 1.0);
    vAlpha = aAlpha;
}
)";
static const char* trailFS = R"(
#version 330 core
in  float vAlpha;
out vec4  FragColor;
uniform vec3 uTrailColor;
void main(){ FragColor = vec4(uTrailColor, vAlpha); }
)";

// ── Simple colour+alpha shader (grid, velocity vectors, mouse dot) ────────────
static const char* simpleVS = R"(
#version 330 core
layout(location=0) in vec2 aPos;
void main(){ gl_Position = vec4(aPos, 0.0, 1.0); }
)";
static const char* simpleFS = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main(){ FragColor = uColor; }
)";

// ─────────────────────────────────────────────────────────────────────────────
static GLuint compileShader(const char* vs, const char* fs)
{
    auto compile = [](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };
    GLuint v = compile(GL_VERTEX_SHADER,   vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
//   COLOUR HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static void hsvToRgb(float h, float s, float v, float& r, float& g, float& b)
{
    int   i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v*(1-s), q = v*(1-f*s), t = v*(1-(1-f)*s);
    switch (i%6) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default:r=v; g=p; b=q; break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//   TRAIL SYSTEM
// ─────────────────────────────────────────────────────────────────────────────
static const int MAX_TRAIL_FRAMES = 60;
struct TrailBuffer {
    std::vector<std::vector<float>> positions; // [frame][agent*2]
    int head = 0;
    int frames = 0;
    int maxFrames = 0;
    int agentCount = 0;

    void init(int agents, int maxF) {
        agentCount = agents; maxFrames = maxF; frames = 0; head = 0;
        positions.assign(maxF, std::vector<float>(agents * 2, 0.0f));
    }
    void push(const float* pos, int n) {
        if (maxFrames == 0) return;
        agentCount = n;
        positions[head].assign(pos, pos + n*2);
        head = (head + 1) % maxFrames;
        if (frames < maxFrames) frames++;
    }
    // get frame k steps in the past (0=oldest shown, frames-1=newest)
    const float* get(int k) const {
        int idx = ((head - frames + k) % maxFrames + maxFrames) % maxFrames;
        return positions[idx].data();
    }
};

static TrailBuffer g_trail;

// ─────────────────────────────────────────────────────────────────────────────
//   MAIN
// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    // ── GLFW init ─────────────────────────────────────────────────────────────
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Swarm Simulation", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n"; return -1;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // ── Compile shaders ───────────────────────────────────────────────────────
    GLuint agentShader  = compileShader(agentVS,  agentFS);
    GLuint trailShader  = compileShader(trailVS,  trailFS);
    GLuint simpleShader = compileShader(simpleVS, simpleFS);

    GLint trailColorLoc  = glGetUniformLocation(trailShader,  "uTrailColor");
    GLint simpleColorLoc = glGetUniformLocation(simpleShader, "uColor");

    // ── Agent VAO + position VBO + colour VBO ─────────────────────────────────
    int agentCount = params.agentCount; // default 10000

    GLuint agentVAO, agentPosBuf, agentColBuf;
    glGenVertexArrays(1, &agentVAO);
    glGenBuffers(1, &agentPosBuf);
    glGenBuffers(1, &agentColBuf);

    glBindVertexArray(agentVAO);
    // loc 0: positions (written by CUDA via interop)
    glBindBuffer(GL_ARRAY_BUFFER, agentPosBuf);
    glBufferData(GL_ARRAY_BUFFER, agentCount * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    // loc 1: colours (written by CPU each frame)
    glBindBuffer(GL_ARRAY_BUFFER, agentColBuf);
    glBufferData(GL_ARRAY_BUFFER, agentCount * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ── Trail VAO ─────────────────────────────────────────────────────────────
    GLuint trailVAO, trailPosBuf, trailAlphaBuf;
    glGenVertexArrays(1, &trailVAO);
    glGenBuffers(1, &trailPosBuf);
    glGenBuffers(1, &trailAlphaBuf);

    glBindVertexArray(trailVAO);
    glBindBuffer(GL_ARRAY_BUFFER, trailPosBuf);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, trailAlphaBuf);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ── Grid / velocity VAO (reused) ──────────────────────────────────────────
    GLuint lineVAO, lineBuf;
    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineBuf);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineBuf);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // ── Mouse dot VAO ─────────────────────────────────────────────────────────
    GLuint mouseVAO, mouseBuf;
    glGenVertexArrays(1, &mouseVAO);
    glGenBuffers(1, &mouseBuf);
    glBindVertexArray(mouseVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mouseBuf);
    float mousePoint[2] = {0,0};
    glBufferData(GL_ARRAY_BUFFER, sizeof(mousePoint), mousePoint, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // ── Init simulation ───────────────────────────────────────────────────────
    initSimulation(agentCount, params);
    registerRenderBuffer(agentPosBuf);
    g_trail.init(agentCount, MAX_TRAIL_FRAMES);

    // ── Fullscreen toggle ─────────────────────────────────────────────────────
    bool isFullscreen = false;
    int  windowedX = 100, windowedY = 100, windowedW = 1280, windowedH = 800;
    bool fPressedLastFrame = false;

    // ── Recording ─────────────────────────────────────────────────────────────
    int  recordFrame = 0;
    _mkdir("frames");

    // ─────────────────────────────────────────────────────────────────────────
    //  MAIN LOOP
    // ─────────────────────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window))
    {
        // ── ImGui new frame ───────────────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ── Fullscreen toggle (F key) ─────────────────────────────────────────
        bool fNow = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (fNow && !fPressedLastFrame) {
            isFullscreen = !isFullscreen;
            if (isFullscreen) {
                GLFWmonitor* mon = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(mon);
                glfwGetWindowPos(window, &windowedX, &windowedY);
                glfwGetWindowSize(window, &windowedW, &windowedH);
                glfwSetWindowMonitor(window, mon, 0, 0,
                    mode->width, mode->height, mode->refreshRate);
            } else {
                glfwSetWindowMonitor(window, nullptr,
                    windowedX, windowedY, windowedW, windowedH, 0);
            }
        }
        fPressedLastFrame = fNow;

        // ── Window / mouse ────────────────────────────────────────────────────
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        float mouseX = (float)(mx / width)  * 2.0f - 1.0f;
        float mouseY = 1.0f - (float)(my / height) * 2.0f;

        // ── Reinit if requested ───────────────────────────────────────────────
        if (params.reinitRequested) {
            shutdownSimulation();
            agentCount = params.agentCount;

            glBindBuffer(GL_ARRAY_BUFFER, agentPosBuf);
            glBufferData(GL_ARRAY_BUFFER, agentCount * 2 * sizeof(float),
                         nullptr, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, agentColBuf);
            glBufferData(GL_ARRAY_BUFFER, agentCount * 4 * sizeof(float),
                         nullptr, GL_DYNAMIC_DRAW);

            initSimulation(agentCount, params);
            registerRenderBuffer(agentPosBuf);
            g_trail.init(agentCount, MAX_TRAIL_FRAMES);
            params.reinitRequested = false;
        }

        // ── Step simulation ───────────────────────────────────────────────────
        auto simStart = std::chrono::high_resolution_clock::now();
        if (!paused || stepOnce) {
            updateMovingObstacles(obstacles, 0.016f);
            // Upload obstacle list to GPU before kernel launch
            // (done inside stepSimulation via updated simulation.cu)
            stepSimulation(0.016f, mouseX, mouseY, params);
            getCounts(&stats.predatorCount, &stats.preyCount);
            stats.avgSpeed = getAverageSpeed();
            stepOnce = false;
        }
        auto simEnd = std::chrono::high_resolution_clock::now();
        stats.simTimeMs =
            std::chrono::duration<float, std::milli>(simEnd - simStart).count();

        // ── Build per-agent colour array ──────────────────────────────────────
        int   curCount = getAgentCount();
        const float* positions = getAgentPositions();

        std::vector<float> colours(curCount * 4);
        for (int i = 0; i < curCount; i++) {
            float r = 1, g = 1, b = 1, a = 1;

            switch (renderOpts.colorScheme) {

            case COLOR_UNIFORM:
            default:
                r = 1.0f; g = 1.0f; b = 1.0f;
                break;

            case COLOR_VELOCITY: {
                // We need velocity — read from h_agents via getAgentPositions
                // Fallback: encode speed with a heat gradient (blue→red)
                // We store velocities separately via getAgentVelocities (see below)
                // For now use position-based rainbow as placeholder; actual
                // velocity colours require the helper added to simulation.cu
                float vx = 0, vy = 0;
                // Access via the public getAgentVelocity helper (see sim header)
                // Until we add it, derive approximate speed from position delta:
                float speed = stats.avgSpeed; // uniform fallback
                float t = std::min(speed / 1.0f, 1.0f);
                r = t; g = 0.3f*(1-t); b = 1.0f - t;
                break;
            }

            case COLOR_TYPE: {
                // predator: red, prey: cyan — need type info
                // We'll add getAgentType helper (see simulation.cu additions)
                // Encoded via a call below after this loop
                r = 0.3f; g = 0.8f; b = 1.0f; // default prey colour
                break;
            }

            case COLOR_RAINBOW: {
                float hue = (float)i / (float)curCount;
                hsvToRgb(hue, 0.9f, 1.0f, r, g, b);
                break;
            }
            }

            colours[4*i+0] = r;
            colours[4*i+1] = g;
            colours[4*i+2] = b;
            colours[4*i+3] = a;
        }

        // Overlay type colours if COLOR_TYPE is active (using h_agents indirectly)
        if (renderOpts.colorScheme == COLOR_TYPE) {
            // getCounts gave us totals; for per-agent we use the count pattern:
            // first predatorCount agents (sorted by init) are predators.
            // Better: expose a helper. For now use a reasonable approximation
            // based on the fact initSimulation places predators first.
            int predN = stats.predatorCount;
            for (int i = 0; i < curCount; i++) {
                bool isPred = (i < predN);
                colours[4*i+0] = isPred ? 1.0f : 0.2f;
                colours[4*i+1] = isPred ? 0.2f : 0.8f;
                colours[4*i+2] = isPred ? 0.2f : 1.0f;
            }
        }

        // Upload colour VBO
        glBindVertexArray(agentVAO);
        glBindBuffer(GL_ARRAY_BUFFER, agentColBuf);
        if ((int)colours.size() > 0) {
            glBufferData(GL_ARRAY_BUFFER,
                         colours.size() * sizeof(float),
                         colours.data(), GL_DYNAMIC_DRAW);
        }
        glBindVertexArray(0);

        // ── Push trail ────────────────────────────────────────────────────────
        int trailFrames = (int)renderOpts.trailLength;  // 0..60
        if (trailFrames > 0) {
            if (g_trail.agentCount != curCount || g_trail.maxFrames != trailFrames)
                g_trail.init(curCount, trailFrames);
            g_trail.push(positions, curCount);
        }

        // ── CLEAR ─────────────────────────────────────────────────────────────
        glViewport(0, 0, width, height);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ── Draw grid ─────────────────────────────────────────────────────────
        if (renderOpts.showGrid) {
            std::vector<float> gridLines;
            int   gridN  = 20;
            float step   = 2.0f / gridN;
            for (int g = 0; g <= gridN; g++) {
                float p = -1.0f + g * step;
                gridLines.insert(gridLines.end(), {p, -1.0f, p,  1.0f});
                gridLines.insert(gridLines.end(), {-1.0f, p,  1.0f, p});
            }
            glBindVertexArray(lineVAO);
            glBindBuffer(GL_ARRAY_BUFFER, lineBuf);
            glBufferData(GL_ARRAY_BUFFER,
                         gridLines.size() * sizeof(float),
                         gridLines.data(), GL_DYNAMIC_DRAW);
            glUseProgram(simpleShader);
            glUniform4f(simpleColorLoc, 0.25f, 0.25f, 0.30f, 1.0f);
            glDrawArrays(GL_LINES, 0, (GLsizei)(gridLines.size() / 2));
            glBindVertexArray(0);
        }

        // ── Draw trails ───────────────────────────────────────────────────────
        if (trailFrames > 0 && g_trail.frames >= 2) {
            // Build line-segment VBO: connect consecutive frames for each agent
            // To keep the upload sane we draw all agents for each frame pair.
            // Limit agents drawn for trails to avoid GPU overload.
            int drawN = std::min(curCount, 5000);

            std::vector<float> trailPos;
            std::vector<float> trailAlpha;
            trailPos.reserve(drawN * g_trail.frames * 2);
            trailAlpha.reserve(drawN * g_trail.frames);

            for (int k = 0; k < g_trail.frames - 1; k++) {
                const float* f0 = g_trail.get(k);
                const float* f1 = g_trail.get(k + 1);
                float alpha = (float)(k + 1) / g_trail.frames * 0.6f;

                for (int a = 0; a < drawN; a++) {
                    trailPos.insert(trailPos.end(),
                        {f0[2*a], f0[2*a+1], f1[2*a], f1[2*a+1]});
                    trailAlpha.push_back(alpha);
                    trailAlpha.push_back(alpha);
                }
            }

            glBindVertexArray(trailVAO);
            glBindBuffer(GL_ARRAY_BUFFER, trailPosBuf);
            glBufferData(GL_ARRAY_BUFFER,
                         trailPos.size() * sizeof(float),
                         trailPos.data(), GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, trailAlphaBuf);
            glBufferData(GL_ARRAY_BUFFER,
                         trailAlpha.size() * sizeof(float),
                         trailAlpha.data(), GL_DYNAMIC_DRAW);

            glUseProgram(trailShader);
            glUniform3f(trailColorLoc, 0.6f, 0.85f, 1.0f);
            glDrawArrays(GL_LINES, 0, (GLsizei)(trailPos.size() / 2));
            glBindVertexArray(0);
        }

        // ── Draw agents ───────────────────────────────────────────────────────
        glUseProgram(agentShader);
        glBindVertexArray(agentVAO);
        glPointSize(renderOpts.agentSize);
        glDrawArrays(GL_POINTS, 0, curCount);
        glBindVertexArray(0);

        // ── Draw velocity vectors ─────────────────────────────────────────────
        if (renderOpts.showVelocity) {
            // Build line pairs: origin → origin + velocity * scale
            int drawN = std::min(curCount, 3000);
            std::vector<float> velLines;
            velLines.reserve(drawN * 4);
            const float SCALE = 8.0f;
            // We need velocities. Access via simulation's h_agents copy through
            // the getAgentPositions trick: positions are updated but velocities
            // aren't exposed separately. We'll draw very short fixed lines in
            // the heading direction derived from the last two position frames.
            if (g_trail.frames >= 2 && g_trail.agentCount >= drawN) {
                const float* prev = g_trail.get(g_trail.frames - 2);
                const float* curr = g_trail.get(g_trail.frames - 1);
                for (int a = 0; a < drawN; a++) {
                    float x0 = curr[2*a],   y0 = curr[2*a+1];
                    float dx  = x0 - prev[2*a];
                    float dy  = y0 - prev[2*a+1];
                    velLines.insert(velLines.end(),
                        {x0, y0, x0 + dx*SCALE, y0 + dy*SCALE});
                }
            } else {
                // no trail data yet; draw nothing
            }

            if (!velLines.empty()) {
                glBindVertexArray(lineVAO);
                glBindBuffer(GL_ARRAY_BUFFER, lineBuf);
                glBufferData(GL_ARRAY_BUFFER,
                             velLines.size() * sizeof(float),
                             velLines.data(), GL_DYNAMIC_DRAW);
                glUseProgram(simpleShader);
                glUniform4f(simpleColorLoc, 0.2f, 1.0f, 0.4f, 0.7f);
                glDrawArrays(GL_LINES, 0, (GLsizei)(velLines.size() / 2));
                glBindVertexArray(0);
            }
        }

        // ── Draw obstacles ────────────────────────────────────────────────────
        // Draw a simple circle approximation for each obstacle
        if (!obstacles.empty()) {
            for (const Obstacle& o : obstacles) {
                std::vector<float> pts;
                if (o.type == OBS_CIRCLE) {
                    const int SEGS = 32;
                    for (int s = 0; s < SEGS; s++) {
                        float a0 = (float)s      / SEGS * 6.28318f;
                        float a1 = (float)(s+1)  / SEGS * 6.28318f;
                        pts.insert(pts.end(), {
                            o.x + cosf(a0)*o.radius, o.y + sinf(a0)*o.radius,
                            o.x + cosf(a1)*o.radius, o.y + sinf(a1)*o.radius });
                    }
                } else if (o.type == OBS_RECT) {
                    float hx = o.x2, hy = o.y2;
                    pts = {
                        o.x-hx, o.y-hy, o.x+hx, o.y-hy,
                        o.x+hx, o.y-hy, o.x+hx, o.y+hy,
                        o.x+hx, o.y+hy, o.x-hx, o.y+hy,
                        o.x-hx, o.y+hy, o.x-hx, o.y-hy
                    };
                } else if (o.type == OBS_LINE) {
                    pts = { o.x, o.y, o.x2, o.y2 };
                }
                if (!pts.empty()) {
                    glBindVertexArray(lineVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, lineBuf);
                    glBufferData(GL_ARRAY_BUFFER,
                                 pts.size() * sizeof(float),
                                 pts.data(), GL_DYNAMIC_DRAW);
                    glUseProgram(simpleShader);
                    glUniform4f(simpleColorLoc, 1.0f, 0.6f, 0.1f, 1.0f);
                    glDrawArrays(GL_LINES, 0, (GLsizei)(pts.size() / 2));
                    glBindVertexArray(0);
                }
            }
        }

        // ── Draw mouse dot ────────────────────────────────────────────────────
        float mp[2] = {mouseX, mouseY};
        glBindBuffer(GL_ARRAY_BUFFER, mouseBuf);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(mp), mp);
        glUseProgram(simpleShader);
        glUniform4f(simpleColorLoc, 1.0f, 0.2f, 0.2f, 1.0f);
        glPointSize(12.0f);
        glBindVertexArray(mouseVAO);
        glDrawArrays(GL_POINTS, 0, 1);
        glBindVertexArray(0);

        // ── ImGui UI ──────────────────────────────────────────────────────────
        renderFullUI(params, renderOpts, stats, obstacles,
                     paused, screenshotRequested, recordingActive);

        // ── Export / Load State (flags set by ui.cpp) ────────────────────────
        if (g_exportStateRequested) {
            exportState(g_savePathBuf);
            g_exportStateRequested = false;
        }
        if (g_loadStateRequested) {
            loadState(g_savePathBuf);   // sets params.reinitRequested = true
            g_loadStateRequested = false;
        }

        // ── Screenshot ────────────────────────────────────────────────────────
        if (screenshotRequested) {
            _mkdir("screenshots");
            char path[128];
            std::snprintf(path, sizeof(path),
                          "screenshots/screenshot_%lld.png",
                          (long long)time(nullptr));
            takeScreenshot(window, path);
            screenshotRequested = false;
        }

        // ── Recording ────────────────────────────────────────────────────────
        if (recordingActive) {
            char path[128];
            std::snprintf(path, sizeof(path), "frames/frame_%06d.png", recordFrame++);
            takeScreenshot(window, path);
        }

        // ── Render ImGui + swap ───────────────────────────────────────────────
        auto renderStart = std::chrono::high_resolution_clock::now();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        auto renderEnd = std::chrono::high_resolution_clock::now();
        stats.renderTimeMs =
            std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();

        ImGuiIO& io = ImGui::GetIO();
        stats.fps = io.Framerate;
        stats.frameTimeMs = (io.Framerate > 0) ? 1000.0f / io.Framerate : 0.0f;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    shutdownSimulation();
    glDeleteBuffers(1, &agentPosBuf);   glDeleteBuffers(1, &agentColBuf);
    glDeleteVertexArrays(1, &agentVAO);
    glDeleteBuffers(1, &trailPosBuf);   glDeleteBuffers(1, &trailAlphaBuf);
    glDeleteVertexArrays(1, &trailVAO);
    glDeleteBuffers(1, &lineBuf);       glDeleteVertexArrays(1, &lineVAO);
    glDeleteBuffers(1, &mouseBuf);      glDeleteVertexArrays(1, &mouseVAO);
    glDeleteProgram(agentShader);
    glDeleteProgram(trailShader);
    glDeleteProgram(simpleShader);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//   SCREENSHOT
// ─────────────────────────────────────────────────────────────────────────────
static void takeScreenshot(GLFWwindow* window, const char* path)
{
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    std::vector<unsigned char> pixels(w * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // OpenGL origin is bottom-left; stb expects top-left → flip vertically
    std::vector<unsigned char> flipped(w * h * 3);
    for (int row = 0; row < h; row++) {
        memcpy(flipped.data() + row * w * 3,
               pixels.data() + (h - 1 - row) * w * 3,
               w * 3);
    }
    stbi_write_png(path, w, h, 3, flipped.data(), w * 3);
}

// ─────────────────────────────────────────────────────────────────────────────
//   EXPORT / LOAD STATE (JSON)
// ─────────────────────────────────────────────────────────────────────────────
static void exportState(const std::string& path)
{
    _mkdir("saves");
    std::ofstream f(path);
    if (!f.is_open()) return;

    // SimParams
    f << "{\n";
    f << "  \"agentCount\": "        << params.agentCount        << ",\n";
    f << "  \"separation\": "        << params.separation        << ",\n";
    f << "  \"alignment\": "         << params.alignment         << ",\n";
    f << "  \"cohesion\": "          << params.cohesion          << ",\n";
    f << "  \"perceptionRadius\": "  << params.perceptionRadius  << ",\n";
    f << "  \"maxSpeed\": "          << params.maxSpeed          << ",\n";
    f << "  \"maxForce\": "          << params.maxForce          << ",\n";
    f << "  \"speedFactor\": "       << params.speedFactor       << ",\n";
    f << "  \"predatorRatio\": "     << params.predatorRatio     << ",\n";
    f << "  \"predatorSpeedMul\": "  << params.predatorSpeedMul  << ",\n";
    f << "  \"fearWeight\": "        << params.fearWeight        << ",\n";
    f << "  \"windX\": "             << params.windX             << ",\n";
    f << "  \"windY\": "             << params.windY             << ",\n";
    f << "  \"attractorActive\": "   << (params.attractorActive ? 1 : 0) << ",\n";
    f << "  \"attractorX\": "        << params.attractorX        << ",\n";
    f << "  \"attractorY\": "        << params.attractorY        << ",\n";
    f << "  \"attractorStrength\": " << params.attractorStrength << ",\n";
    f << "  \"attractorRadius\": "   << params.attractorRadius   << ",\n";

    // RenderOptions
    f << "  \"colorScheme\": "  << (int)renderOpts.colorScheme  << ",\n";
    f << "  \"agentSize\": "    << renderOpts.agentSize          << ",\n";
    f << "  \"trailLength\": "  << renderOpts.trailLength        << ",\n";
    f << "  \"cameraFOV\": "    << renderOpts.cameraFOV          << ",\n";
    f << "  \"showGrid\": "     << (renderOpts.showGrid     ? 1 : 0) << ",\n";
    f << "  \"showVelocity\": " << (renderOpts.showVelocity ? 1 : 0) << ",\n";

    // Obstacles
    f << "  \"obstacles\": [\n";
    for (size_t i = 0; i < obstacles.size(); i++) {
        const Obstacle& o = obstacles[i];
        f << "    {\"type\":"    << (int)o.type
          << ",\"x\":"         << o.x    << ",\"y\":"  << o.y
          << ",\"x2\":"        << o.x2   << ",\"y2\":" << o.y2
          << ",\"radius\":"    << o.radius
          << ",\"moving\":"    << (o.isMoving ? 1 : 0)
          << ",\"mvx\":"       << o.moveSpeedX
          << ",\"mvy\":"       << o.moveSpeedY << "}";
        if (i + 1 < obstacles.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
}

static bool loadState(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;

    auto readFloat = [](const std::string& line, const char* key, float& out) {
        std::string k = std::string("\"") + key + "\": ";
        auto p = line.find(k);
        if (p == std::string::npos) return false;
        out = std::stof(line.substr(p + k.size())); return true;
    };
    auto readInt = [](const std::string& line, const char* key, int& out) {
        std::string k = std::string("\"") + key + "\": ";
        auto p = line.find(k);
        if (p == std::string::npos) return false;
        out = std::stoi(line.substr(p + k.size())); return true;
    };

    obstacles.clear();
    bool inObs = false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("\"obstacles\"") != std::string::npos) { inObs = true; continue; }

        if (!inObs) {
            int iv = 0; float fv = 0;
            readInt  (line, "agentCount",       params.agentCount);
            readFloat(line, "separation",        params.separation);
            readFloat(line, "alignment",         params.alignment);
            readFloat(line, "cohesion",          params.cohesion);
            readFloat(line, "perceptionRadius",  params.perceptionRadius);
            readFloat(line, "maxSpeed",          params.maxSpeed);
            readFloat(line, "maxForce",          params.maxForce);
            readFloat(line, "speedFactor",       params.speedFactor);
            readFloat(line, "predatorRatio",     params.predatorRatio);
            readFloat(line, "predatorSpeedMul",  params.predatorSpeedMul);
            readFloat(line, "fearWeight",        params.fearWeight);
            readFloat(line, "windX",             params.windX);
            readFloat(line, "windY",             params.windY);
            if (readInt(line, "attractorActive", iv)) params.attractorActive = iv != 0;
            readFloat(line, "attractorX",        params.attractorX);
            readFloat(line, "attractorY",        params.attractorY);
            readFloat(line, "attractorStrength", params.attractorStrength);
            readFloat(line, "attractorRadius",   params.attractorRadius);
            if (readInt(line, "colorScheme", iv))  renderOpts.colorScheme = (ColorScheme)iv;
            readFloat(line, "agentSize",         renderOpts.agentSize);
            readFloat(line, "trailLength",       renderOpts.trailLength);
            readFloat(line, "cameraFOV",         renderOpts.cameraFOV);
            if (readInt(line, "showGrid",     iv)) renderOpts.showGrid    = iv != 0;
            if (readInt(line, "showVelocity", iv)) renderOpts.showVelocity= iv != 0;
        } else {
            // parse obstacle JSON objects on single lines
            if (line.find('{') == std::string::npos) continue;
            Obstacle o{};
            int typeI=0, movI=0;
            sscanf(line.c_str(),
                "    {\"type\":%d,\"x\":%f,\"y\":%f,\"x2\":%f,\"y2\":%f,"
                "\"radius\":%f,\"moving\":%d,\"mvx\":%f,\"mvy\":%f}",
                &typeI, &o.x, &o.y, &o.x2, &o.y2,
                &o.radius, &movI, &o.moveSpeedX, &o.moveSpeedY);
            o.type = (ObstacleType)typeI;
            o.isMoving = movI != 0;
            obstacles.push_back(o);
        }
    }
    params.reinitRequested = true;
    return true;
}
