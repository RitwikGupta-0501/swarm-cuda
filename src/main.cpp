#include "camera.h"
#include "error.h"
#include "renderer.h"
#include "time_stats.h"
#include "simulation.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <string>

using namespace swarm;

namespace {

struct AppCtx {
  Renderer renderer;
  TimeStats stats{120};
  int agentCount = 1000;
  float timeSeconds = 0.0f;
  bool showMouseCapture = false;
};

void framebufferSizeCb(GLFWwindow* w, int width, int height) {
  auto* ctx = static_cast<AppCtx*>(glfwGetWindowUserPointer(w));
  if (!ctx) return;
  ctx->renderer.resize(width, height);
}

void cursorPosCb(GLFWwindow* w, double x, double y) {
  auto* ctx = static_cast<AppCtx*>(glfwGetWindowUserPointer(w));
  if (!ctx) return;
  ctx->renderer.camera().onMouseMove(x, y);
}

void mouseButtonCb(GLFWwindow* w, int button, int action, int mods) {
  auto* ctx = static_cast<AppCtx*>(glfwGetWindowUserPointer(w));
  if (!ctx) return;
  ctx->renderer.camera().onMouseButton(button, action, mods);
}

void scrollCb(GLFWwindow* w, double /*xoff*/, double yoff) {
  auto* ctx = static_cast<AppCtx*>(glfwGetWindowUserPointer(w));
  if (!ctx) return;
  double cx = 0.0, cy = 0.0;
  glfwGetCursorPos(w, &cx, &cy);
  ctx->renderer.camera().onScroll(yoff, cx, cy);
}

void keyCb(GLFWwindow* w, int key, int scancode, int action, int mods) {
  auto* ctx = static_cast<AppCtx*>(glfwGetWindowUserPointer(w));
  if (!ctx) return;

  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
    if (key == GLFW_KEY_TAB) {
      const auto m = ctx->renderer.camera().mode();
      ctx->renderer.setCameraMode(
          m == CameraMode::Ortho2D ? CameraMode::Perspective25D : CameraMode::Ortho2D);
    }
    if (key == GLFW_KEY_1) ctx->renderer.setVizMode(VizMode::Uniform);
    if (key == GLFW_KEY_2) ctx->renderer.setVizMode(VizMode::VelocityHeat);
    if (key == GLFW_KEY_3) ctx->renderer.setVizMode(VizMode::Direction);
    if (key == GLFW_KEY_4) ctx->renderer.setVizMode(VizMode::RainbowTime);
    if (key == GLFW_KEY_5) ctx->renderer.setVizMode(VizMode::TypeBased);

    if (key == GLFW_KEY_G)
      ctx->renderer.setGlowEnabled(!ctx->renderer.glowEnabled());

    // Task 1.8: V  — toggle velocity-vector debug overlay
    if (key == GLFW_KEY_V)
      ctx->renderer.setShowVelocityVectors(!ctx->renderer.showVelocityVectors());

    // Task 1.6: T  — toggle motion trails
    if (key == GLFW_KEY_T)
      ctx->renderer.setShowTrails(!ctx->renderer.showTrails());

    // Task 1.7: C  — toggle GPU frustum culling
    if (key == GLFW_KEY_C)
      ctx->renderer.setFrustumCullingEnabled(!ctx->renderer.frustumCullingEnabled());
  }

  ctx->renderer.camera().onKey(key, scancode, action, mods);
}

} // namespace

int main() {
  installGlfwErrorCallback();
  if (!glfwInit()) {
    SWARM_FATAL("glfwInit failed");
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#ifndef NDEBUG
  glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

  GLFWwindow* window = glfwCreateWindow(1280, 720, "Swarm Visualizer", nullptr, nullptr);
  if (!window) SWARM_FATAL("glfwCreateWindow failed");
  glfwMakeContextCurrent(window);
  glfwSwapInterval(0); // uncapped for perf testing

  if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
    SWARM_FATAL("Failed to load OpenGL via GLAD");
  }

  // installGlDebugCallbackIfAvailable();

  AppCtx ctx;
  glfwSetWindowUserPointer(window, &ctx);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCb);
  glfwSetCursorPosCallback(window, cursorPosCb);
  glfwSetMouseButtonCallback(window, mouseButtonCb);
  glfwSetScrollCallback(window, scrollCb);
  glfwSetKeyCallback(window, keyCb);

  int w = 1280, h = 720;
  glfwGetFramebufferSize(window, &w, &h);

  RendererConfig cfg{};
  cfg.maxAgents = 1000;

  std::string err;
  if (!ctx.renderer.init(cfg, w, h, &err)) {
    SWARM_FATAL(err.empty() ? "Renderer init failed" : err);
  }

  initSimulation(cfg.maxAgents);
  ctx.agentCount = getAgentCount();
  ctx.renderer.uploadAgentTypes(getAgentTypesArray(), ctx.agentCount);

  auto last = std::chrono::high_resolution_clock::now();

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    const auto now = std::chrono::high_resolution_clock::now();
    const double dt = std::chrono::duration<double>(now - last).count();
    last = now;

    float rawDt = static_cast<float>(dt);
    float safeDt = std::clamp(rawDt, 0.001f, 0.033f);
    ctx.timeSeconds += safeDt;
    ctx.stats.push(safeDt);

    const FrameStats s = ctx.stats.stats();

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    int currentW, currentH;
    glfwGetFramebufferSize(window, &currentW, &currentH);
    float mouseX = (static_cast<float>(mx) / currentW) * 2.0f - 1.0f;
    float mouseY = 1.0f - (static_cast<float>(my) / currentH) * 2.0f;

    void* interopRes = ctx.renderer.getInteropHandle().cudaGraphicsResource;
    stepSimulation(safeDt, mouseX, mouseY, interopRes);

    // Note: CUDA simulation should map/write/unmap the shared VBO here.
    // This demo does not populate agents yet; renderer still runs and overlays work.
    ctx.renderer.render(ctx.agentCount, ctx.timeSeconds, s);

    glfwSwapBuffers(window);
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
