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
#include "renderer.h"

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
//   SHADERS (Removed)
// ─────────────────────────────────────────────────────────────────────────────

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
//   TRAIL SYSTEM (Removed)
// ─────────────────────────────────────────────────────────────────────────────

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
    initSimulation(agentCount, params);
    registerRenderBuffer(renderer.getAgentVbo());

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

        // ── Reinit if requested ───────────────────────────────────────────────
        if (params.reinitRequested) {
            shutdownSimulation();
            agentCount = params.agentCount;

            initSimulation(agentCount, params);
            registerRenderBuffer(renderer.getAgentVbo());

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

        // ── Render simulation ──────────────────────────────────────────────────
        int curCount = getAgentCount();

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

        renderer.render(curCount, static_cast<float>(glfwGetTime()), swarm::FrameStats{});

        swarm::CameraMatrices camMats = renderer.camera().matrices(0.0f);
        stats.cameraMode = static_cast<int>(camMats.mode);
        stats.camX       = camMats.cameraPos.x;
        stats.camY       = camMats.cameraPos.y;
        stats.camZ       = camMats.cameraPos.z;
        stats.camZoom    = camMats.zoom;

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
