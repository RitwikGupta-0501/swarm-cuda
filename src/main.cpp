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

#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>
#include <cstdio>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "renderer.h"
#include "scenarios.h"

// ─── Globals ──────────────────────────────────────────────────────────────────
SimParams            params;
RenderOptions        renderOpts;
SimStats             stats;
std::vector<Obstacle> obstacles;
ScenarioState         scenarioState;

bool paused              = false;
bool stepOnce            = false;
bool screenshotRequested = false;
bool recordingActive     = false;

// Flags written by ui.cpp, consumed here
bool g_exportStateRequested = false;
bool g_loadStateRequested   = false;

// Path buffer (shared with ui.cpp via a simple extern trick;
// we redeclare it here as the authoritative definition)
char g_savePathBuf[128] = "saves/state.json";

// ─── Forward declarations ────────────────────────────────────────────────────
static void takeScreenshot(GLFWwindow* window, const char* path);
static void exportState(const std::string& path);
static bool loadState (const std::string& path);

// ─────────────────────────────────────────────────────────────────────────────
//   CALLBACKS
// ─────────────────────────────────────────────────────────────────────────────
static void cursorPosCb(GLFWwindow* w, double x, double y) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    auto* renderer = static_cast<swarm::Renderer*>(glfwGetWindowUserPointer(w));
    if (renderer) renderer->camera().onMouseMove(x, y);
}

static void mouseButtonCb(GLFWwindow* w, int button, int action, int mods) {
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL)) {
        int width, height;
        glfwGetWindowSize(w, &width, &height);
        double cx, cy;
        glfwGetCursorPos(w, &cx, &cy);
        params.attractorX = (float)(cx / width) * 2.0f - 1.0f;
        params.attractorY = 1.0f - (float)(cy / height) * 2.0f;
    }

    auto* renderer = static_cast<swarm::Renderer*>(glfwGetWindowUserPointer(w));
    if (renderer) renderer->camera().onMouseButton(button, action, mods);
}

static void scrollCb(GLFWwindow* w, double /*xoff*/, double yoff) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    auto* renderer = static_cast<swarm::Renderer*>(glfwGetWindowUserPointer(w));
    if (renderer) {
        double cx = 0.0, cy = 0.0;
        glfwGetCursorPos(w, &cx, &cy);
        renderer->camera().onScroll(yoff, cx, cy);
    }
}

static void keyCb(GLFWwindow* w, int key, int scancode, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    auto* renderer = static_cast<swarm::Renderer*>(glfwGetWindowUserPointer(w));
    if (!renderer) return;

    // Toggle camera mode on 'C' press
    if (action == GLFW_PRESS && key == GLFW_KEY_C) {
        const auto m = renderer->camera().mode();
        renderer->setCameraMode(
            m == swarm::CameraMode::Ortho2D ? swarm::CameraMode::Perspective25D : swarm::CameraMode::Ortho2D);
    }

    if (io.WantCaptureKeyboard && io.WantTextInput) return;
    renderer->camera().onKey(key, scancode, action, mods);
}

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
    glEnable(GL_PROGRAM_POINT_SIZE);

    // ── Callbacks (must be set BEFORE ImGui init so ImGui can chain them) ──
    swarm::Renderer renderer;
    glfwSetWindowUserPointer(window, &renderer);
    glfwSetCursorPosCallback(window, cursorPosCb);
    glfwSetMouseButtonCallback(window, mouseButtonCb);
    glfwSetScrollCallback(window, scrollCb);
    glfwSetKeyCallback(window, keyCb);

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    swarm::RendererConfig cfg{};
    cfg.maxAgents = params.agentCount;
    std::string err;

    if (!renderer.init(cfg, 1280, 800, &err)) {
        std::cerr << "Renderer init failed: " << err << "\n";
        return -1;
    }
    renderer.setFrustumCullingEnabled(true);   // Enable culling
    renderer.setCameraMode(swarm::CameraMode::Ortho2D);  // Default to 2D

    int agentCount = params.agentCount;

    // ── Init simulation ───────────────────────────────────────────────────────
    SwarmEngine engine;
    engine.init(agentCount, params);
    engine.registerRenderBuffer(renderer.getAgentVbo());

    std::vector<uint32_t> initialTypes(agentCount, 0);
    int initialNumPreds = static_cast<int>(agentCount * params.predatorRatio);
    for (int i = 0; i < initialNumPreds; ++i) {
        initialTypes[i] = 1;
    }
    renderer.uploadAgentTypes(initialTypes.data(), agentCount);

    // ── Fullscreen toggle ─────────────────────────────────────────────────────
    bool isFullscreen = false;
    int  windowedX = 100, windowedY = 100, windowedW = 1280, windowedH = 800;
    bool fPressedLastFrame = false;

    // ── Recording ─────────────────────────────────────────────────────────────
    int  recordFrame = 0;
    std::filesystem::create_directories("frames");

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
        static int lastW = 0, lastH = 0;
        if (width != lastW || height != lastH) {
            renderer.resize(width, height);
            lastW = width; lastH = height;
        }
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        float mouseX = (float)(mx / width)  * 2.0f - 1.0f;
        float mouseY = 1.0f - (float)(my / height) * 2.0f;

        // Bind attractor to cursor if enabled
        if (params.attractorActive && params.attractorBindToCursor) {
            params.attractorX = mouseX;
            params.attractorY = mouseY;
        }

        // ── Reinit if requested ───────────────────────────────────────────────
        if (params.reinitRequested) {
            // 1. Tear down the simulation and unmap its CUDA resources
            engine.shutdown();
            agentCount = params.agentCount;

            // 2. Resize Renderer's OpenGL buffers (and its internal CUDA interop handle)
            std::string err;
            if (!renderer.resizeAgentBuffers(agentCount, &err)) {
                std::cerr << "Failed to resize agent buffers: " << err << "\n";
            }

            // 3. Re-initialize simulation (allocates new CUDA arrays)
            engine.init(agentCount, params);

            // 4. Re-register the newly sized OpenGL VBO with the simulation
            engine.registerRenderBuffer(renderer.getAgentVbo());

            // 5. Restore metadata (agent types for rendering)
            std::vector<uint32_t> types(agentCount, 0);
            int numPreds = static_cast<int>(agentCount * params.predatorRatio);
            for (int i = 0; i < numPreds; ++i) {
                types[i] = 1;
            }
            renderer.uploadAgentTypes(types.data(), agentCount);

            params.reinitRequested = false;
        }

        // ── Step simulation ───────────────────────────────────────────────────
        auto simStart = std::chrono::high_resolution_clock::now();

        // Fixed timestep: simulation advances 16ms per frame regardless of
        // wall-clock time. This ensures deterministic behavior but means
        // simulation speed varies with framerate. Use params.speedFactor
        // to compensate.
        constexpr float FIXED_DT = 0.016f;

        if (!paused || stepOnce) {
            updateMovingObstacles(obstacles, FIXED_DT);

            // FIX: Upload the updated obstacle list to the GPU before the kernel launch!
            engine.updateGPUObstacles(obstacles);

            engine.step(FIXED_DT, mouseX, mouseY, params);
            engine.getKernelProfileTimes(stats.spatialHashTimeMs, stats.physicsKernelTimeMs);

            engine.getCounts(&stats.predatorCount, &stats.preyCount);
            stats.avgSpeed = engine.getAverageSpeed();
            stepOnce = false;
        }

        if (engine.isAgentCountDirty()) {
            int newCount = engine.getAgentCount();
            std::string err;
            if (!renderer.resizeAgentBuffers(newCount, &err)) {
                std::cerr << "Failed to resize agent buffers: " << err << "\n";
            }
            engine.unregisterRenderBuffer();
            engine.registerRenderBuffer(renderer.getAgentVbo());

            std::vector<uint32_t> types(newCount, 0);
            int numPreds = static_cast<int>(newCount * params.predatorRatio);
            for (int i = 0; i < numPreds; ++i) {
                types[i] = 1;
            }
            renderer.uploadAgentTypes(types.data(), newCount);
            engine.clearAgentCountDirty();
        }

        // Update active scenario (wind, migration target, etc.)
        if (scenarioState.running) {
            updateScenario(scenarioState, params, obstacles, FIXED_DT);
        }

        auto simEnd = std::chrono::high_resolution_clock::now();
        stats.simTimeMs =
            std::chrono::duration<float, std::milli>(simEnd - simStart).count();

        // ── Render simulation ──────────────────────────────────────────────────
        int curCount = engine.getAgentCount();

        switch (renderOpts.colorScheme) {
            case COLOR_UNIFORM:  renderer.setVizMode(swarm::VizMode::Uniform); break;
            case COLOR_VELOCITY: renderer.setVizMode(swarm::VizMode::VelocityHeat); break;
            case COLOR_TYPE:     renderer.setVizMode(swarm::VizMode::TypeBased); break;
            case COLOR_RAINBOW:  renderer.setVizMode(swarm::VizMode::RainbowTime); break;
        }

        renderer.setTrailLength(static_cast<int>(renderOpts.trailLength));
        renderer.setShowTrails(renderOpts.trailLength > 0.0f);

        renderer.setShowVelocityVectors(renderOpts.showVelocity);

        renderer.setAgentSize(renderOpts.agentSize);
        renderer.camera().setFov(renderOpts.cameraFOV);

        renderer.setShowGrid(renderOpts.showGrid);

        auto renderStart = std::chrono::high_resolution_clock::now();
        renderer.render(curCount, static_cast<float>(glfwGetTime()), swarm::FrameStats{});
        auto renderEnd = std::chrono::high_resolution_clock::now();

        swarm::CameraMatrices camMats = renderer.camera().matrices(0.0f);
        stats.cameraMode = static_cast<int>(camMats.mode);
        stats.camX       = camMats.cameraPos.x;
        stats.camY       = camMats.cameraPos.y;
        stats.camZ       = camMats.cameraPos.z;
        stats.camZoom    = camMats.zoom;

        // ── ImGui UI ──────────────────────────────────────────────────────────
        renderFullUI(engine, params, renderOpts, stats, obstacles, scenarioState,
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
            std::filesystem::create_directories("screenshots");
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
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        stats.renderTimeMs =
            std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();

        ImGuiIO& io = ImGui::GetIO();
        stats.fps = io.Framerate;
        stats.frameTimeMs = (io.Framerate > 0) ? 1000.0f / io.Framerate : 0.0f;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    engine.shutdown();
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
    std::filesystem::create_directories("saves");
    std::ofstream f(path);
    if (!f.is_open()) return;

    nlohmann::json j;
    
    // SimParams
    j["agentCount"]        = params.agentCount;
    j["separation"]        = params.separation;
    j["alignment"]         = params.alignment;
    j["cohesion"]          = params.cohesion;
    j["perceptionRadius"]  = params.perceptionRadius;
    j["maxSpeed"]          = params.maxSpeed;
    j["maxForce"]          = params.maxForce;
    j["speedFactor"]       = params.speedFactor;
    j["predatorRatio"]     = params.predatorRatio;
    j["predatorSpeedMul"]  = params.predatorSpeedMul;
    j["fearWeight"]        = params.fearWeight;
    j["windX"]             = params.windX;
    j["windY"]             = params.windY;
    j["attractorActive"]   = params.attractorActive;
    j["attractorX"]        = params.attractorX;
    j["attractorY"]        = params.attractorY;
    j["attractorStrength"] = params.attractorStrength;
    j["attractorRadius"]   = params.attractorRadius;

    // RenderOptions
    j["colorScheme"]  = (int)renderOpts.colorScheme;
    j["agentSize"]    = renderOpts.agentSize;
    j["trailLength"]  = renderOpts.trailLength;
    j["cameraFOV"]    = renderOpts.cameraFOV;
    j["showGrid"]     = renderOpts.showGrid;
    j["showVelocity"] = renderOpts.showVelocity;

    // Obstacles
    nlohmann::json obsArray = nlohmann::json::array();
    for (const auto& o : obstacles) {
        obsArray.push_back({
            {"type",   (int)o.type},
            {"x",      o.x},
            {"y",      o.y},
            {"x2",     o.x2},
            {"y2",     o.y2},
            {"radius", o.radius},
            {"moving", o.isMoving},
            {"mvx",    o.moveSpeedX},
            {"mvy",    o.moveSpeedY}
        });
    }
    j["obstacles"] = obsArray;

    f << j.dump(4);
}

static bool loadState(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;
    nlohmann::json j;
    try { f >> j; } catch (...) { return false; }

    if (j.contains("agentCount"))        params.agentCount        = j["agentCount"];
    if (j.contains("separation"))        params.separation        = j["separation"];
    if (j.contains("alignment"))         params.alignment         = j["alignment"];
    if (j.contains("cohesion"))          params.cohesion          = j["cohesion"];
    if (j.contains("perceptionRadius"))  params.perceptionRadius  = j["perceptionRadius"];
    if (j.contains("maxSpeed"))          params.maxSpeed          = j["maxSpeed"];
    if (j.contains("maxForce"))          params.maxForce          = j["maxForce"];
    if (j.contains("speedFactor"))       params.speedFactor       = j["speedFactor"];
    if (j.contains("predatorRatio"))     params.predatorRatio     = j["predatorRatio"];
    if (j.contains("predatorSpeedMul"))  params.predatorSpeedMul  = j["predatorSpeedMul"];
    if (j.contains("fearWeight"))        params.fearWeight        = j["fearWeight"];
    if (j.contains("windX"))             params.windX             = j["windX"];
    if (j.contains("windY"))             params.windY             = j["windY"];
    if (j.contains("attractorActive"))   params.attractorActive   = j["attractorActive"];
    if (j.contains("attractorX"))        params.attractorX        = j["attractorX"];
    if (j.contains("attractorY"))        params.attractorY        = j["attractorY"];
    if (j.contains("attractorStrength")) params.attractorStrength = j["attractorStrength"];
    if (j.contains("attractorRadius"))   params.attractorRadius   = j["attractorRadius"];

    if (j.contains("colorScheme"))  renderOpts.colorScheme = (ColorScheme)j["colorScheme"].get<int>();
    if (j.contains("agentSize"))    renderOpts.agentSize = j["agentSize"];
    if (j.contains("trailLength"))  renderOpts.trailLength = j["trailLength"];
    if (j.contains("cameraFOV"))    renderOpts.cameraFOV = j["cameraFOV"];
    if (j.contains("showGrid"))     renderOpts.showGrid = j["showGrid"];
    if (j.contains("showVelocity")) renderOpts.showVelocity = j["showVelocity"];

    if (j.contains("obstacles") && j["obstacles"].is_array()) {
        obstacles.clear();
        for (const auto& o : j["obstacles"]) {
            Obstacle obs{};
            if (o.contains("type"))   obs.type = (ObstacleType)o["type"].get<int>();
            if (o.contains("x"))      obs.x = o["x"];
            if (o.contains("y"))      obs.y = o["y"];
            if (o.contains("x2"))     obs.x2 = o["x2"];
            if (o.contains("y2"))     obs.y2 = o["y2"];
            if (o.contains("radius")) obs.radius = o["radius"];
            if (o.contains("moving")) obs.isMoving = o["moving"];
            if (o.contains("mvx"))    obs.moveSpeedX = o["mvx"];
            if (o.contains("mvy"))    obs.moveSpeedY = o["mvy"];
            obstacles.push_back(obs);
        }
    }

    params.reinitRequested = true;
    return true;
}
