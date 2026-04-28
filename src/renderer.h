#pragma once

#include "agent_layout.h"
#include "camera.h"
#include "time_stats.h"

#include <glad/glad.h>

#include <cstdint>
#include <string>

namespace swarm {

struct RendererConfig {
  int  maxAgents         = 1'000'000;
  bool enableAdditiveGlow = false;   // initial state of glow; toggleable at runtime via G key
  int  trailLength       = 20;       // history slots per agent (0 = trails disabled)
};

enum class VizMode : int {
  Uniform      = 0,
  VelocityHeat = 1,
  Direction    = 2,
  RainbowTime  = 3,
  TypeBased    = 4,   // prey (0) = teal, predator (1) = red-orange
};

struct CudaInteropHandle {
  void* cudaGraphicsResource = nullptr;
};

// ── Bloom FBO bundle ─────────────────────────────────────────────────────────
// All textures are created at the current viewport size by rebuildBloomTargets().
struct BloomTargets {
  GLuint sceneFbo   = 0;   // main scene render target
  GLuint colorTex   = 0;   // RGB16F scene colour
  GLuint depthRbo   = 0;   // depth renderbuffer (only main FBO needs depth)

  GLuint brightFbo  = 0;   // bright-pass output
  GLuint brightTex  = 0;   // RGB16F bright pixels only

  GLuint pingFbo[2] = {0, 0};  // ping-pong blur (half-res for speed)
  GLuint pingTex[2] = {0, 0};

  int width  = 0;
  int height = 0;
};

class Renderer {
public:
  Renderer() = default;
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  bool init(const RendererConfig& cfg, int viewportW, int viewportH, std::string* outError);
  void resize(int viewportW, int viewportH);

  void setCameraMode(CameraMode mode);
  Camera& camera() { return mCamera; }

  void setVizMode(VizMode mode) { mVizMode = mode; }
  VizMode vizMode() const { return mVizMode; }

  // ── Feature toggles (runtime, hotkey driven) ──────────────────────────────
  void setShowVelocityVectors(bool v) { mShowVelocityVectors = v; }
  bool showVelocityVectors()   const  { return mShowVelocityVectors; }

  void setShowTrails(bool v) { mShowTrails = v; }
  bool showTrails()   const  { return mShowTrails; }
  void setTrailLength(int length) { mCfg.trailLength = length; }

  void setFrustumCullingEnabled(bool v) { mFrustumCullingEnabled = v; }
  bool frustumCullingEnabled()   const  { return mFrustumCullingEnabled; }

  void setGlowEnabled(bool v) { mGlowEnabled = v; }
  bool glowEnabled()   const  { return mGlowEnabled; }

  void setShowGrid(bool show) { mShowGrid = show; }
  bool showGrid() const { return mShowGrid; }

  // ── Agent type API ────────────────────────────────────────────────────────
  // Upload an array of per-agent types (0=prey, 1=predator) to the GPU.
  // Call once after simulation sets up agent roles.
  // count must be <= maxAgents.
  void uploadAgentTypes(const uint32_t* types, int count);

  void setAgentSize(float size) { mAgentSize = size; }

  // Expose the type SSBO so the CUDA/CPU simulation can write directly.
  GLuint getAgentTypeSsbo() const { return mAgentTypeSsbo; }

  // ── Main render API ───────────────────────────────────────────────────────
  void render(int agent_count);
  void render(int agent_count, float timeSeconds, const FrameStats& frameStats);

  bool resizeAgentBuffers(int newMaxAgents, std::string* outError = nullptr);

  GLuint getAgentVbo()        const { return mAgentVbo; }
  CudaInteropHandle getInteropHandle() const { return mInterop; }

private:
  RendererConfig mCfg{};
  Camera         mCamera{};
  VizMode        mVizMode = VizMode::VelocityHeat;

  // ── Feature flags ─────────────────────────────────────────────────────────
  bool mShowVelocityVectors   = false;
  bool mShowTrails            = false;
  bool mFrustumCullingEnabled = true;
  bool mGlowEnabled           = false;
  bool mShowGrid = false;

  // ── Core camera UBO ───────────────────────────────────────────────────────
  GLuint mCameraUbo = 0;

  // ── Agent instanced geometry ──────────────────────────────────────────────
  float mAgentSize = 2.0f;
  GLuint mAgentVao     = 0;
  GLuint mAgentBaseVbo = 0;
  GLuint mAgentVbo     = 0;   // position+velocity, shared with CUDA (16 bytes/agent)

  GLuint mAgentProgram  = 0;
  GLuint mPointsProgram = 0;

  // ── Agent type SSBO (separate from CUDA VBO — no interop conflict) ────────
  GLuint mAgentTypeSsbo = 0;  // uint32_t per agent, binding 5

  // ── Debug draw module ─────────────────────────────────────────────────────
  class DebugDraw* mDebug = nullptr;

  // ── Task 1.8: Debug velocity-vector overlay ───────────────────────────────
  GLuint mDebugVecProgram = 0;
  GLuint mDummyVao        = 0;   // empty VAO for SSBO-driven draws

  // ── Task 1.7: GPU Frustum Culling ─────────────────────────────────────────
  GLuint mCullProgram          = 0;
  GLuint mVisibleAgentSsbo     = 0;
  GLuint mVisibleAgentTypeSsbo = 0;  // Binding 6
  GLuint mVisibleAgentIdSsbo   = 0;  // Binding 7
  GLuint mDrawCmdBuf           = 0;
  GLuint mTrailDrawCmdBuf      = 0;  // Indirect draw for trails
  GLuint mCullAgentVao         = 0;
  GLuint mCullAgentBaseVbo = 0;

  // ── Task 1.6: Motion Trails ───────────────────────────────────────────────
  GLuint   mTrailUpdateProgram = 0;
  GLuint   mTrailRenderProgram = 0;
  GLuint   mTrailSsbo          = 0;
  uint32_t mTrailFrameIdx      = 0;

  // ── Glow / Bloom post-process ─────────────────────────────────────────────
  BloomTargets mBloom{};
  GLuint mBloomBrightProgram    = 0;
  GLuint mBloomBlurProgram      = 0;
  GLuint mBloomCompositeProgram = 0;

  // ── CUDA interop ──────────────────────────────────────────────────────────
  CudaInteropHandle mInterop{};

  int mViewportW = 1;
  int mViewportH = 1;

  // ── Private helpers ───────────────────────────────────────────────────────
  void destroyGl();

  bool createCameraUbo(std::string* outError);
  void updateCameraUbo(float timeSeconds);

  bool createAgentPipeline(std::string* outError);
  bool createSharedAgentBuffer(std::string* outError);
  bool createAgentTypeSsbo(std::string* outError);
  bool registerCudaInterop(std::string* outError);

  // Task 1.8
  bool createDebugVecPipeline(std::string* outError);

  // Task 1.7
  bool createCullPipeline(std::string* outError);
  void dispatchCull(int agentCount, const CameraMatrices& cam);

  // Task 1.6
  bool createTrailPipelines(std::string* outError);
  void dispatchTrailUpdate(int agentCount);
  void renderTrails(int agentCount);

  // Glow/Bloom
  bool createBloomPipeline(std::string* outError);
  void rebuildBloomTargets();   // called from init() and resize()
  void destroyBloomTargets();
  void runBloomPasses();        // bright → blur H → blur V → composite
};

} // namespace swarm
