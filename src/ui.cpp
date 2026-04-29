#include "ui.h"
#include "imgui.h"
#include "presets.h"
#include "simulation.h"   // addAgent, convertRandomAgent
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

extern bool stepOnce;

// ── Forward declarations for state I/O (implemented in main.cpp) ─────────────
// We expose them via weak linkage by declaring them here with extern.
// main.cpp defines them as static; to allow ui.cpp to call them we use a
// callback approach — ui.cpp sets flag booleans and main.cpp acts on them.
// (Cleaner than circular includes.)
extern bool g_exportStateRequested;
extern bool g_loadStateRequested;
extern char g_savePathBuf[128];

// ─── Simple original UI ───────────────────────────────────────────────────────
void renderUI(bool& paused, float& speed,
              float& separation, float& alignment, float& cohesion)
{
    ImGui::Begin("Controls");
    ImGui::Checkbox("Pause", &paused);
    ImGui::SliderFloat("Speed",      &speed,      0.1f, 5.0f);
    ImGui::SliderFloat("Separation", &separation, 0.0f, 5.0f);
    ImGui::SliderFloat("Alignment",  &alignment,  0.0f, 5.0f);
    ImGui::SliderFloat("Cohesion",   &cohesion,   0.0f, 5.0f);
    ImGui::End();
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
static const char* colorSchemeNames[]  = {
    "Uniform", "Velocity Heat-map", "Type (Prey/Predator)", "Rainbow"
};
static const char* obstacleTypeNames[] = { "Circle", "Rectangle", "Line" };

static void HelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(450.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// ─── Full-featured UI ─────────────────────────────────────────────────────────
void renderFullUI(SimParams&              params,
                  RenderOptions&          renderOpts,
                  SimStats&               stats,
                  std::vector<Obstacle>&  obstacles,
                  bool&                   paused,
                  bool&                   screenshotRequested,
                  bool&                   recordingActive)
{
    // ── Apply dark style once ─────────────────────────────────────────────────
    static bool styleApplied = false;
    if (!styleApplied) {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 6.0f; s.FrameRounding     = 4.0f;
        s.GrabRounding      = 4.0f; s.ScrollbarRounding = 4.0f;
        s.WindowBorderSize  = 1.0f;
        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg]         = ImVec4(0.10f, 0.10f, 0.13f, 0.92f);
        c[ImGuiCol_TitleBg]          = ImVec4(0.08f, 0.08f, 0.20f, 1.00f);
        c[ImGuiCol_TitleBgActive]    = ImVec4(0.15f, 0.15f, 0.40f, 1.00f);
        c[ImGuiCol_SliderGrab]       = ImVec4(0.25f, 0.55f, 0.90f, 1.00f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.70f, 1.00f, 1.00f);
        c[ImGuiCol_Button]           = ImVec4(0.20f, 0.40f, 0.75f, 0.80f);
        c[ImGuiCol_ButtonHovered]    = ImVec4(0.30f, 0.55f, 0.90f, 1.00f);
        c[ImGuiCol_ButtonActive]     = ImVec4(0.10f, 0.30f, 0.60f, 1.00f);
        c[ImGuiCol_FrameBg]          = ImVec4(0.18f, 0.18f, 0.25f, 1.00f);
        c[ImGuiCol_Header]           = ImVec4(0.20f, 0.40f, 0.70f, 0.55f);
        c[ImGuiCol_HeaderHovered]    = ImVec4(0.25f, 0.50f, 0.85f, 0.80f);
        c[ImGuiCol_HeaderActive]     = ImVec4(0.15f, 0.35f, 0.65f, 1.00f);
        styleApplied = true;
    }

    // ── 1. Performance overlay (top-right) ────────────────────────────────────
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 210.f, 10.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(200.f, 0.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##perf", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::Text("FPS         %6.1f",  stats.fps);
        ImGui::Text("Frame       %5.2f ms", stats.frameTimeMs);
        ImGui::Text("Sim         %5.2f ms", stats.simTimeMs);
        ImGui::Text("Render      %5.2f ms", stats.renderTimeMs);
        ImGui::Separator();
        ImGui::Text("Prey        %d", stats.preyCount);
        ImGui::Text("Predators   %d", stats.predatorCount);
        ImGui::Text("Avg speed   %.3f", stats.avgSpeed);
        ImGui::Separator();
        ImGui::Text("Cam Mode    %s", stats.cameraMode == 0 ? "2D" : "2.5D");
        if (stats.cameraMode == 0) {
            ImGui::Text("Pos         %.2f, %.2f", stats.camX, stats.camY);
            ImGui::Text("Zoom        %.2f", stats.camZoom);
        } else {
            ImGui::Text("Pos         %.1f, %.1f", stats.camX, stats.camY);
            ImGui::Text("Alt         %.1f", stats.camZ);
        }
        ImGui::End();
    }

    // ── 2. Main controls window ───────────────────────────────────────────────
    ImGui::SetNextWindowSize(ImVec2(310.f, 0.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f),  ImGuiCond_FirstUseEver);
    ImGui::Begin("Swarm Controls");

    // ── Simulation ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(paused ? "  Play  " : "  Pause ")) paused = !paused;
        ImGui::SameLine();
        if (ImGui::Button("  Reset  ")) params.reinitRequested = true;
        ImGui::SameLine();
        extern bool stepOnce;
        ImGui::BeginDisabled(!paused);
        if (ImGui::Button(" Step ")) stepOnce = true;
        ImGui::EndDisabled();

        ImGui::Separator();
        int newCount = params.agentCount;
        if (ImGui::SliderInt("Agent Count", &newCount, 1000, 200000)) {
            if (newCount != params.agentCount) {
                params.agentCount      = newCount;
                params.reinitRequested = true;
            }
        }
        HelpMarker("Number of agents. Change triggers re-initialization.");
        ImGui::SliderFloat("Time Scale", &params.speedFactor, 0.1f, 5.0f);
        HelpMarker("Multiplier on simulation dt.");
    }

    // ── Boids ─────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Boids Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Separation",        &params.separation,       0.0f, 5.0f);
        ImGui::SliderFloat("Alignment",         &params.alignment,        0.0f, 5.0f);
        ImGui::SliderFloat("Cohesion",          &params.cohesion,         0.0f, 5.0f);
        ImGui::SliderFloat("Perception Radius", &params.perceptionRadius, 0.05f, 0.5f);
        ImGui::SliderFloat("Max Speed",         &params.maxSpeed,         0.1f, 2.0f);
        ImGui::SliderFloat("Max Force",         &params.maxForce,         0.01f, 0.5f);
    }

    // ── Presets ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Presets")) {
        static const char* presetNames[] = {
            "Bird Flock","Fish School","Chaos","Orbiting","Murmuration"
        };
        static int selectedPreset = -1;
        ImGui::Combo("##preset", &selectedPreset, presetNames, 5);
        ImGui::SameLine();
        if (ImGui::Button("Apply") && selectedPreset >= 0)
            loadPreset(selectedPreset, params);
        ImGui::SameLine();
        if (ImGui::Button("Save"))
            savePreset("custom", params);
        HelpMarker("Select a preset and click Apply. Click Save to write current params.");
    }

    // ── Visual settings ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Visual")) {
        int cs = (int)renderOpts.colorScheme;
        if (ImGui::Combo("Color Scheme", &cs, colorSchemeNames, 4))
            renderOpts.colorScheme = (ColorScheme)cs;

        ImGui::SliderFloat("Agent Size",   &renderOpts.agentSize,   1.0f, 8.0f);

        // Trail length: 0 = off, slider up to MAX_TRAIL_FRAMES (60)
        int trailInt = (int)renderOpts.trailLength;
        if (ImGui::SliderInt("Trail Length", &trailInt, 0, 60))
            renderOpts.trailLength = (float)trailInt;
        HelpMarker("Number of past frames shown as trails (0 = off).");

        ImGui::Checkbox("Show Grid",        &renderOpts.showGrid);
        ImGui::SameLine();
        ImGui::Checkbox("Velocity Vectors", &renderOpts.showVelocity);
        ImGui::SliderFloat("Camera FOV",    &renderOpts.cameraFOV, 20.0f, 120.0f);
        HelpMarker("Camera FOV is passed to renderer.cpp (Person 1).");
    }

    // ── Predator-Prey ─────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Predator / Prey")) {
        if (ImGui::SliderFloat("Predator Ratio", &params.predatorRatio, 0.0f, 0.5f)) {
            params.reinitRequested = true;
        }
        ImGui::SliderFloat("Predator Speed Mul", &params.predatorSpeedMul, 1.0f, 3.0f);
        ImGui::SliderFloat("Fear Weight",        &params.fearWeight,       0.0f, 10.0f);
        HelpMarker("How strongly prey flee from predators.");
        ImGui::Separator();

        if (ImGui::Button("Add Predator")) {
            addAgent(PREDATOR);   // ← real call to simulation.cu
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Prey")) {
            addAgent(PREY);       // ← real call to simulation.cu
        }
        ImGui::SameLine();
        if (ImGui::Button("Convert Random")) {
            convertRandomAgent(); // ← real call to simulation.cu
        }
        ImGui::SameLine(); HelpMarker("Add a single agent or flip a random agent's type.");
    }

    // ── Obstacles ─────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Obstacles")) {
        static int newObsType = OBS_CIRCLE;
        ImGui::Combo("Type", &newObsType, obstacleTypeNames, 3);
        ImGui::SameLine();
        if (ImGui::Button("Add##obs")) {
            Obstacle o{};
            o.type   = (ObstacleType)newObsType;
            o.x      = 0.0f; o.y = 0.0f;
            o.x2     = 0.1f; o.y2 = 0.1f;
            o.radius = 0.1f;
            obstacles.push_back(o);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All##obs")) obstacles.clear();

        ImGui::Separator();
        for (int i = 0; i < (int)obstacles.size(); i++) {
            ImGui::PushID(i);
            char label[32];
            std::snprintf(label, sizeof(label), "Obs %d [%s]", i,
                          obstacleTypeNames[(int)obstacles[i].type]);
            if (ImGui::TreeNode(label)) {
                ImGui::DragFloat2("Position",  &obstacles[i].x,  0.005f, -1.0f, 1.0f);
                if (obstacles[i].type == OBS_CIRCLE) {
                    ImGui::SliderFloat("Radius",   &obstacles[i].radius, 0.02f, 0.4f);
                } else if (obstacles[i].type == OBS_RECT) {
                    ImGui::DragFloat2("Half-Ext.", &obstacles[i].x2,  0.005f, 0.01f, 0.5f);
                } else {
                    ImGui::DragFloat2("End Point", &obstacles[i].x2,  0.005f, -1.0f, 1.0f);
                }
                ImGui::Checkbox("Moving", &obstacles[i].isMoving);
                if (obstacles[i].isMoving)
                    ImGui::DragFloat2("Move Speed", &obstacles[i].moveSpeedX,
                                      0.001f, -0.5f, 0.5f);
                if (ImGui::Button("Remove")) {
                    obstacles.erase(obstacles.begin() + i);
                    ImGui::TreePop(); ImGui::PopID(); break;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    // ── Environment ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Environment")) {
        ImGui::Text("Wind / Current");
        ImGui::SliderFloat("Wind X", &params.windX, -1.0f, 1.0f);
        ImGui::SliderFloat("Wind Y", &params.windY, -1.0f, 1.0f);

        ImGui::Separator();
        ImGui::Text("Attractor / Repulsor");
        ImGui::Checkbox("Active", &params.attractorActive);
        if (params.attractorActive) {
            ImGui::Checkbox("Bind to Cursor", &params.attractorBindToCursor);
            ImGui::SameLine();
            HelpMarker("Field will follow mouse. When OFF, use Ctrl + Left Click in viewport to teleport.");

            ImGui::BeginDisabled(params.attractorBindToCursor);
            ImGui::DragFloat2("Position##att", &params.attractorX, 0.005f, -1.0f, 1.0f);
            ImGui::SliderFloat("Strength",     &params.attractorStrength, -5.0f, 5.0f);
            HelpMarker("Positive = attract, Negative = repel.");
            ImGui::SliderFloat("Radius##att",  &params.attractorRadius, 0.05f, 1.0f);

            ImGui::EndDisabled();
        }
    }

    // ── Export & Record ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Export & Record")) {
        // Screenshot
        if (ImGui::Button("Screenshot (PNG)"))
            screenshotRequested = true;
        HelpMarker("Saves to screenshots/ directory.");
        ImGui::SameLine();

        // Recording toggle
        ImVec4 recCol = recordingActive
            ? ImVec4(0.9f, 0.2f, 0.2f, 1.0f)
            : ImVec4(0.2f, 0.7f, 0.2f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, recCol);
        if (ImGui::Button(recordingActive ? "Stop Recording" : "Start Recording"))
            recordingActive = !recordingActive;
        ImGui::PopStyleColor();
        HelpMarker("Saves sequential PNGs to frames/ directory.\n"
                   "Combine with ffmpeg: ffmpeg -r 60 -i frames/frame_%06d.png out.mp4");

        ImGui::Separator();

        // Export / Load State (full JSON including render options + obstacles)
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##savepath", g_savePathBuf, sizeof(g_savePathBuf));
        ImGui::SameLine();
        if (ImGui::Button("Export State")) {
            g_exportStateRequested = true;   // main.cpp reads this flag
        }
        ImGui::SameLine();
        if (ImGui::Button("Load State")) {
            g_loadStateRequested = true;     // main.cpp reads this flag
        }
        HelpMarker("Export saves ALL params, render options, and obstacles to JSON.\n"
                   "Load restores everything and triggers re-initialization.");

        ImGui::Separator();
        if (ImGui::Button("Export Params")) {
            savePreset("exported", params);  // presets.cpp
        }
        HelpMarker("Appends current params to config/presets.json as 'exported'.");
    }

    ImGui::End();  // Swarm Controls
}
