#include "renderer.h"

#include "debug_draw.h"
#include "error.h"
#include "gl_shader.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <iostream>

#if SWARM_ENABLE_CUDA
#include "interop_cuda_gl.h"
#endif

namespace swarm {
    namespace {
        // ---- Camera UBO (std140) — matches CameraUboStd140 in shader ----
        struct CameraUboStd140 {
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
        glm::mat4 viewProj{1.0f};
        glm::vec4 cameraPos_Time{0, 0, 0, 0};
        glm::vec4 viewport_Zoom_Mode{0, 0, 1, 0};
        };
        static_assert(sizeof(CameraUboStd140) % 16 == 0,
                    "UBO must be 16-byte aligned.");

        // ---- GL DrawArraysIndirectCommand ----
        struct DrawArraysIndirectCommand {
        GLuint count;         // vertex count per instance (3 = triangle, 1 = point)
        GLuint instanceCount; // number of instances — written by compute
        GLuint first;         // = 0
        GLuint baseInstance;  // = 0
        };

        // Compute the world-space AABB visible through the camera (2D AABB).
        // For 2.5D we just use a large conservative box so nothing is wrongly culled.
        struct WorldAabb {
        glm::vec2 min, max;
        };

        WorldAabb cameraWorldAabb(const CameraMatrices &cam, int vpW, int vpH) {
        if (cam.mode == CameraMode::Ortho2D) {
            const float aspect = static_cast<float>(vpW) / static_cast<float>(vpH);
            const float worldHeight = 2.0f / std::max(0.0001f, cam.zoom);
            const float worldWidth = worldHeight * aspect;
            const float halfW = 0.5f * worldWidth;
            const float halfH = 0.5f * worldHeight;
            const glm::vec2 center{cam.cameraPos.x, cam.cameraPos.y};
            // Conservative: add half-diagonal for rotated camera frustum.
            const float pad = std::sqrt(halfW * halfW + halfH * halfH);
            return {center - glm::vec2(pad), center + glm::vec2(pad)};
        } else {
            // Perspective25D: use a very large box so all XY-plane agents pass.
            return {glm::vec2(-1e7f), glm::vec2(1e7f)};
        }
        }
    } // namespace

    // =============================================================================
    // Destructor
    // =============================================================================
    Renderer::~Renderer() {
        destroyGl();
        delete mDebug;
        mDebug = nullptr;
    }

    // =============================================================================
    // Public Init
    // =============================================================================
    bool Renderer::init(const RendererConfig &cfg, int viewportW, int viewportH,
                        std::string *outError) {
        mCfg = cfg;
        mViewportW = viewportW;
        mViewportH = viewportH;

        mCamera.setViewport(viewportW, viewportH);

        mDebug = new DebugDraw();
        if (!mDebug->init(outError))
            return false;
        mDebug->resize(viewportW, viewportH);

        if (!createCameraUbo(outError))
            return false;
        if (!createAgentPipeline(outError))
            return false;
        if (!createSharedAgentBuffer(outError))
            return false;
        if (!createDebugVecPipeline(outError))
            return false; // Task 1.8
        if (!createCullPipeline(outError))
            return false; // Task 1.7
        if (mCfg.trailLength > 0) {
            if (!createTrailPipelines(outError))
            return false; // Task 1.6
        }
        if (!createAgentTypeSsbo(outError))
            return false; // Prey/Predator type
        if (!createBloomPipeline(outError))
            return false; // Glow/Bloom
        rebuildBloomTargets();
        if (!registerCudaInterop(outError))
            return false;
        return true;
    }

    void Renderer::resize(int viewportW, int viewportH) {
        mViewportW = viewportW;
        mViewportH = viewportH;
        mCamera.setViewport(viewportW, viewportH);
        if (mDebug)
            mDebug->resize(viewportW, viewportH);
        rebuildBloomTargets();
    }

    void Renderer::setCameraMode(CameraMode mode) { mCamera.setMode(mode); }

    // =============================================================================
    // destroyGl
    // =============================================================================
    void Renderer::destroyGl() {
        // Original resources.
        if (mAgentProgram)
            glDeleteProgram(mAgentProgram);
        if (mPointsProgram)
            glDeleteProgram(mPointsProgram);
        if (mAgentVao)
            glDeleteVertexArrays(1, &mAgentVao);
        if (mAgentBaseVbo)
            glDeleteBuffers(1, &mAgentBaseVbo);
        if (mAgentVbo)
            glDeleteBuffers(1, &mAgentVbo);
        if (mCameraUbo)
            glDeleteBuffers(1, &mCameraUbo);

        // Task 1.8 — debug vectors.
        if (mDebugVecProgram)
            glDeleteProgram(mDebugVecProgram);
        // mAgentSsbo is an alias binding — no separate buffer to delete.
        if (mDummyVao)
            glDeleteVertexArrays(1, &mDummyVao);

        // Agent type SSBO.
        if (mAgentTypeSsbo)
            glDeleteBuffers(1, &mAgentTypeSsbo);

        // Task 1.7 — culling.
        if (mCullProgram)
            glDeleteProgram(mCullProgram);
        if (mVisibleAgentSsbo)
            glDeleteBuffers(1, &mVisibleAgentSsbo);
        if (mDrawCmdBuf)
            glDeleteBuffers(1, &mDrawCmdBuf);
        if (mCullAgentVao)
            glDeleteVertexArrays(1, &mCullAgentVao);
        if (mCullAgentBaseVbo)
            glDeleteBuffers(1, &mCullAgentBaseVbo);

        // Task 1.6 — trails.
        if (mTrailUpdateProgram)
            glDeleteProgram(mTrailUpdateProgram);
        if (mTrailRenderProgram)
            glDeleteProgram(mTrailRenderProgram);
        if (mTrailSsbo)
            glDeleteBuffers(1, &mTrailSsbo);

        mAgentProgram = 0;
        mPointsProgram = 0;
        mAgentVao = 0;
        mAgentBaseVbo = 0;
        mAgentVbo = 0;
        mCameraUbo = 0;
        mDebugVecProgram = 0;
        mDummyVao = 0;
        mAgentTypeSsbo = 0;
        mCullProgram = 0;
        mVisibleAgentSsbo = 0;
        mDrawCmdBuf = 0;
        mCullAgentVao = 0;
        mCullAgentBaseVbo = 0;
        mTrailUpdateProgram = 0;
        mTrailRenderProgram = 0;
        mTrailSsbo = 0;

        if (mVisibleAgentTypeSsbo)
            glDeleteBuffers(1, &mVisibleAgentTypeSsbo);
        if (mVisibleAgentIdSsbo)
            glDeleteBuffers(1, &mVisibleAgentIdSsbo);
        if (mTrailDrawCmdBuf)
            glDeleteBuffers(1, &mTrailDrawCmdBuf);
        mVisibleAgentTypeSsbo = 0;
        mVisibleAgentIdSsbo = 0;
        mTrailDrawCmdBuf = 0;

        // Bloom cleanup.
        if (mBloomBrightProgram)
            glDeleteProgram(mBloomBrightProgram);
        if (mBloomBlurProgram)
            glDeleteProgram(mBloomBlurProgram);
        if (mBloomCompositeProgram)
            glDeleteProgram(mBloomCompositeProgram);
        destroyBloomTargets();
        mBloomBrightProgram = 0;
        mBloomBlurProgram = 0;
        mBloomCompositeProgram = 0;

        #if SWARM_ENABLE_CUDA
        if (mInterop.cudaGraphicsResource) {
            cudaUnregisterAgentBuffer(reinterpret_cast<cudaGraphicsResource *>(
                mInterop.cudaGraphicsResource));
            mInterop.cudaGraphicsResource = nullptr;
        }
        #endif
    }

    // =============================================================================
    // Camera UBO
    // =============================================================================
    bool Renderer::createCameraUbo(std::string *outError) {
        SWARM_GL_CALL(glCreateBuffers(1, &mCameraUbo));
        if (!mCameraUbo) {
            if (outError)
            *outError = "Failed to create Camera UBO.";
            return false;
        }
        SWARM_GL_CALL(glNamedBufferData(mCameraUbo, sizeof(CameraUboStd140), nullptr,
                                        GL_DYNAMIC_DRAW));
        SWARM_GL_CALL(glBindBufferBase(GL_UNIFORM_BUFFER, 0, mCameraUbo));
        return true;
    }

    void Renderer::updateCameraUbo(float timeSeconds) {
        const CameraMatrices cam = mCamera.matrices(timeSeconds);
        CameraUboStd140 u{};
        u.view = cam.view;
        u.proj = cam.proj;
        u.viewProj = cam.viewProj;
        u.cameraPos_Time = glm::vec4(cam.cameraPos, timeSeconds);
        u.viewport_Zoom_Mode =
            glm::vec4(static_cast<float>(mViewportW), static_cast<float>(mViewportH),
                        cam.zoom, static_cast<float>(static_cast<int>(cam.mode)));
        SWARM_GL_CALL(
            glNamedBufferSubData(mCameraUbo, 0, sizeof(CameraUboStd140), &u));
    }

    // =============================================================================
    // Agent Render Pipeline (existing)
    // =============================================================================
    bool Renderer::createAgentPipeline(std::string *outError) {
        ShaderProgram agent;
        if (!agent.loadFromFiles("shaders/agent.vert", "shaders/agent.frag",
                                outError)) {
            std::cerr << "Agent shader error: " << (outError ? *outError : "unknown")
                    << "\n";
            return false;
        }

        mAgentProgram = agent.release();
        std::cerr << "Agent shader loaded successfully\n";

        ShaderProgram pts;
        if (!pts.loadFromFiles("shaders/points.vert", "shaders/points.frag",
                                outError))
            return false;
        mPointsProgram = pts.release();

        // Base arrow geometry in XY plane, pointing +X in model space.
        // Scaled down to match the new [-1, 1] normalized coordinate space.
        const float s = 0.003f;
        const float base[] = {10.0f * s, 0.0f,      -6.0f * s,
                                4.0f * s,  -6.0f * s, -4.0f * s};

        SWARM_GL_CALL(glCreateVertexArrays(1, &mAgentVao));
        SWARM_GL_CALL(glCreateBuffers(1, &mAgentBaseVbo));
        SWARM_GL_CALL(
            glNamedBufferData(mAgentBaseVbo, sizeof(base), base, GL_STATIC_DRAW));

        // Binding 0: base geometry (vec2).
        SWARM_GL_CALL(glVertexArrayVertexBuffer(mAgentVao, 0, mAgentBaseVbo, 0,
                                                sizeof(float) * 2));
        SWARM_GL_CALL(glEnableVertexArrayAttrib(mAgentVao, 0));
        SWARM_GL_CALL(
            glVertexArrayAttribFormat(mAgentVao, 0, 2, GL_FLOAT, GL_FALSE, 0));
        SWARM_GL_CALL(glVertexArrayAttribBinding(mAgentVao, 0, 0));

        return true;
    }

    // =============================================================================
    // Shared Agent Buffer (existing) + SSBO alias
    // =============================================================================
    bool Renderer::createSharedAgentBuffer(std::string *outError) {
        SWARM_GL_CALL(glCreateBuffers(1, &mAgentVbo));
        if (!mAgentVbo) {
            if (outError)
            *outError = "Failed to create shared Agent VBO.";
            return false;
        }

        const GLsizeiptr bytes = static_cast<GLsizeiptr>(mCfg.maxAgents) *
                                static_cast<GLsizeiptr>(sizeof(RenderAgent));
        SWARM_GL_CALL(glNamedBufferData(mAgentVbo, bytes, nullptr, GL_DYNAMIC_DRAW));

        // Binding 1: instance buffer (RenderAgent), stride 16 bytes.
        glVertexArrayVertexBuffer(mAgentVao, 1, mAgentVbo, 0, sizeof(RenderAgent));

        // iPosition (location 1).
        glEnableVertexArrayAttrib(mAgentVao, 1);
        glVertexArrayAttribFormat(mAgentVao, 1, 2, GL_FLOAT, GL_FALSE,
                                    offsetof(RenderAgent, position_x));
        glVertexArrayAttribBinding(mAgentVao, 1, 1);
        glVertexArrayBindingDivisor(mAgentVao, 1, 1);

        // iVelocity (location 2).
        glEnableVertexArrayAttrib(mAgentVao, 2);
        glVertexArrayAttribFormat(mAgentVao, 2, 2, GL_FLOAT, GL_FALSE,
                                    offsetof(RenderAgent, velocity_x));
        glVertexArrayAttribBinding(mAgentVao, 2, 1);
        glVertexArrayBindingDivisor(mAgentVao, 1, 1);

        return true;
    }

    bool Renderer::registerCudaInterop(std::string *outError) {
        #if SWARM_ENABLE_CUDA
        cudaGraphicsResource *res = nullptr;
        if (!cudaRegisterAgentBuffer(mAgentVbo, &res, outError))
            return false;
        mInterop.cudaGraphicsResource = res;
        return true;
        #else
        (void)outError;
        return true;
        #endif
    }

    // =============================================================================
    // Task 1.8 — Debug Velocity Vectors
    // =============================================================================
    bool Renderer::createDebugVecPipeline(std::string *outError) {
        ShaderProgram p;
        if (!p.loadFromFiles("shaders/debug_vec.vert", "shaders/debug_vec.frag",
                            outError))
            return false;
        mDebugVecProgram = p.release();

        // A single empty VAO satisfies the GL core-profile requirement for
        // glDrawArraysInstanced even when all data comes from SSBOs.
        SWARM_GL_CALL(glCreateVertexArrays(1, &mDummyVao));
        return true;
    }

    // =============================================================================
    // Task 1.7 — GPU Frustum Culling
    // =============================================================================
    bool Renderer::createCullPipeline(std::string *outError) {
        ShaderProgram comp;
        if (!comp.loadCompute("shaders/cull.comp", outError))
            return false;
        mCullProgram = comp.release();

        // Visible agent SSBO (same size as the full agent buffer).
        const GLsizeiptr agentBytes = static_cast<GLsizeiptr>(mCfg.maxAgents) *
                                        static_cast<GLsizeiptr>(sizeof(RenderAgent));
        SWARM_GL_CALL(glCreateBuffers(1, &mVisibleAgentSsbo));
        SWARM_GL_CALL(glNamedBufferData(mVisibleAgentSsbo, agentBytes, nullptr,
                                        GL_DYNAMIC_COPY));

        // Indirect draw command buffer.
        const DrawArraysIndirectCommand initCmd{3, 0, 0,
                                                0}; // vertCount=3, instanceCount=0
        SWARM_GL_CALL(glCreateBuffers(1, &mDrawCmdBuf));
        SWARM_GL_CALL(glNamedBufferData(mDrawCmdBuf,
                                        sizeof(DrawArraysIndirectCommand), &initCmd,
                                        GL_DYNAMIC_DRAW));

        // VAO that sources instanced attributes from the visible-agent SSBO.
        // Scaled down to match the new [-1, 1] normalized coordinate space.
        const float s = 0.003f;
        const float base[] = {10.0f * s, 0.0f,      -6.0f * s,
                                4.0f * s,  -6.0f * s, -4.0f * s};
        SWARM_GL_CALL(glCreateBuffers(1, &mCullAgentBaseVbo));
        SWARM_GL_CALL(
            glNamedBufferData(mCullAgentBaseVbo, sizeof(base), base, GL_STATIC_DRAW));

        // Secondary visible buffers (types and original IDs).
        const GLsizeiptr typeBytes =
            static_cast<GLsizeiptr>(mCfg.maxAgents) * sizeof(uint32_t);
        SWARM_GL_CALL(glCreateBuffers(1, &mVisibleAgentTypeSsbo));
        SWARM_GL_CALL(glNamedBufferData(mVisibleAgentTypeSsbo, typeBytes, nullptr,
                                        GL_DYNAMIC_COPY));
        SWARM_GL_CALL(glCreateBuffers(1, &mVisibleAgentIdSsbo));
        SWARM_GL_CALL(glNamedBufferData(mVisibleAgentIdSsbo, typeBytes, nullptr,
                                        GL_DYNAMIC_COPY));

        // Trail indirect command buffer: { vertCount, instanceCount, first,
        // baseInstance } vertCount = trailLength, instances = current surviving
        // agents
        const DrawArraysIndirectCommand trailCmd{
            static_cast<uint32_t>(mCfg.trailLength), 0, 0, 0};
        SWARM_GL_CALL(glCreateBuffers(1, &mTrailDrawCmdBuf));
        SWARM_GL_CALL(glNamedBufferData(mTrailDrawCmdBuf,
                                        sizeof(DrawArraysIndirectCommand), &trailCmd,
                                        GL_DYNAMIC_DRAW));

        SWARM_GL_CALL(glCreateVertexArrays(1, &mCullAgentVao));

        // Binding 0: base geometry.
        SWARM_GL_CALL(glVertexArrayVertexBuffer(mCullAgentVao, 0, mCullAgentBaseVbo,
                                                0, sizeof(float) * 2));
        SWARM_GL_CALL(glEnableVertexArrayAttrib(mCullAgentVao, 0));
        SWARM_GL_CALL(
            glVertexArrayAttribFormat(mCullAgentVao, 0, 2, GL_FLOAT, GL_FALSE, 0));
        SWARM_GL_CALL(glVertexArrayAttribBinding(mCullAgentVao, 0, 0));

        // Binding 1: instanced per-agent data from visible-agent SSBO.
        SWARM_GL_CALL(glVertexArrayVertexBuffer(mCullAgentVao, 1, mVisibleAgentSsbo,
                                                0, sizeof(RenderAgent)));
        SWARM_GL_CALL(glEnableVertexArrayAttrib(mCullAgentVao, 1));
        SWARM_GL_CALL(glVertexArrayAttribFormat(mCullAgentVao, 1, 2, GL_FLOAT,
                                                GL_FALSE,
                                                offsetof(RenderAgent, position_x)));
        SWARM_GL_CALL(glVertexArrayAttribBinding(mCullAgentVao, 1, 1));
        SWARM_GL_CALL(glVertexArrayBindingDivisor(mCullAgentVao, 1, 1));

        SWARM_GL_CALL(glEnableVertexArrayAttrib(mCullAgentVao, 2));
        SWARM_GL_CALL(glVertexArrayAttribFormat(mCullAgentVao, 2, 2, GL_FLOAT,
                                                GL_FALSE,
                                                offsetof(RenderAgent, velocity_x)));
        SWARM_GL_CALL(glVertexArrayAttribBinding(mCullAgentVao, 2, 1));
        SWARM_GL_CALL(glVertexArrayBindingDivisor(mCullAgentVao, 1, 1));

        return true;
    }

    void Renderer::dispatchCull(int agentCount, const CameraMatrices &cam) {
        // Reset instanceCount to 0, keep vertCount = 3.
        const DrawArraysIndirectCommand resetCmd{3, 0, 0, 0};
        SWARM_GL_CALL(glNamedBufferSubData(
            mDrawCmdBuf, 0, sizeof(DrawArraysIndirectCommand), &resetCmd));

        const WorldAabb aabb = cameraWorldAabb(cam, mViewportW, mViewportH);

        glUseProgram(mCullProgram);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mAgentVbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, mVisibleAgentSsbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, mDrawCmdBuf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mAgentTypeSsbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, mVisibleAgentTypeSsbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, mVisibleAgentIdSsbo);

        glUniform2f(glGetUniformLocation(mCullProgram, "uCullMin"), aabb.min.x,
                    aabb.min.y);
        glUniform2f(glGetUniformLocation(mCullProgram, "uCullMax"), aabb.max.x,
                    aabb.max.y);
        glUniform1ui(glGetUniformLocation(mCullProgram, "uAgentCount"),
                    static_cast<GLuint>(agentCount));

        const GLuint groups = (static_cast<GLuint>(agentCount) + 255u) / 256u;
        SWARM_GL_CALL(glDispatchCompute(groups, 1, 1));

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // Sync both draw commands (agents and trails) and the instance buffers.
        // We need to copy instanceCount from mDrawCmdBuf[1] to mTrailDrawCmdBuf[1].
        // Both buffers use the same struct layout.
        glCopyNamedBufferSubData(mDrawCmdBuf, mTrailDrawCmdBuf, sizeof(uint32_t),
                                sizeof(uint32_t), sizeof(uint32_t));

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT |
                        GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    }

    // =============================================================================
    // Task 1.6 — Motion Trails
    // =============================================================================
    bool Renderer::createTrailPipelines(std::string * outError) {
        ShaderProgram upd;
        if (!upd.loadCompute("shaders/trail_update.comp", outError))
        return false;
        mTrailUpdateProgram = upd.release();

        ShaderProgram rend;
        if (!rend.loadFromFiles("shaders/trail.vert", "shaders/trail.frag",
                                outError))
        return false;
        mTrailRenderProgram = rend.release();

        // Trail ring-buffer: maxAgents × trailLength × 2 floats.
        const GLsizeiptr trailBytes = static_cast<GLsizeiptr>(mCfg.maxAgents) *
                                    static_cast<GLsizeiptr>(mCfg.trailLength) *
                                    2 * sizeof(float);
        SWARM_GL_CALL(glCreateBuffers(1, &mTrailSsbo));
        SWARM_GL_CALL(
            glNamedBufferData(mTrailSsbo, trailBytes, nullptr, GL_DYNAMIC_COPY));

        return true;
    }

    void Renderer::dispatchTrailUpdate(int agentCount) {
        glUseProgram(mTrailUpdateProgram);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mAgentVbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mTrailSsbo);

        glUniform1ui(glGetUniformLocation(mTrailUpdateProgram, "uAgentCount"),
                    static_cast<GLuint>(agentCount));
        glUniform1ui(glGetUniformLocation(mTrailUpdateProgram, "uTrailLength"),
                    static_cast<GLuint>(mCfg.trailLength));
        glUniform1ui(glGetUniformLocation(mTrailUpdateProgram, "uFrameIdx"),
                    mTrailFrameIdx);

        const GLuint groups = (static_cast<GLuint>(agentCount) + 255u) / 256u;
        SWARM_GL_CALL(glDispatchCompute(groups, 1, 1));
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        ++mTrailFrameIdx;
    }

    void Renderer::renderTrails(int agentCount) {
        glUseProgram(mTrailRenderProgram);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, mTrailSsbo);

        glUniform1ui(glGetUniformLocation(mTrailRenderProgram, "uTrailLength"),
                    static_cast<GLuint>(mCfg.trailLength));
        glUniform1ui(glGetUniformLocation(mTrailRenderProgram, "uFrameIdx"),
                    mTrailFrameIdx > 0 ? mTrailFrameIdx - 1 : 0);
        glUniform1i(glGetUniformLocation(mTrailRenderProgram, "uMode"),
                    static_cast<int>(mVizMode));

        // Use a dummy VAO — GL core profile requires a VAO even when all data
        // is sourced from SSBOs via gl_VertexID / gl_InstanceID.
        glBindVertexArray(mDummyVao);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDisable(GL_DEPTH_TEST);

        // If culling is enabled, use the compacted original IDs from binding 7
        // and draw indirectly using mTrailDrawCmdBuf.
        bool useCulling = mFrustumCullingEnabled && mCullProgram;
        glUniform1i(glGetUniformLocation(mTrailRenderProgram, "uCullingEnabled"),
                    useCulling ? 1 : 0);

        if (useCulling) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, mVisibleAgentIdSsbo);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, mTrailDrawCmdBuf);
        glDrawArraysIndirect(GL_LINE_STRIP, nullptr);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        } else {
        // Draw trailLength vertices per agent instance for all agents.
        glDrawArraysInstanced(GL_LINE_STRIP, 0,
                                static_cast<GLsizei>(mCfg.trailLength),
                                static_cast<GLsizei>(agentCount));
        }
    }

    // =============================================================================
    // Render Entry Points
    // =============================================================================
    void Renderer::render(int agentCount, float timeSeconds,
                            const FrameStats &frameStats) {
        agentCount = std::max(0, std::min(agentCount, mCfg.maxAgents));

        mCamera.update(frameStats.dtSeconds > 0.0
                        ? static_cast<float>(frameStats.dtSeconds)
                        : 0.016f);
        updateCameraUbo(timeSeconds);

        const CameraMatrices cam = mCamera.matrices(timeSeconds);

        if (mGlowEnabled && mBloom.sceneFbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, mBloom.sceneFbo);
        } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        glViewport(0, 0, mViewportW, mViewportH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ── Grid / Axes ─────────────────────────────────────────────────────────
        if (mDebug && mShowGrid) {
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 1, -1, "debug_grid_axes");
            mDebug->drawGridAndAxes(cam);
            glPopDebugGroup();
        }

        // ── Task 1.6: Trail — update ring buffer, then render BEFORE agents ────
        if (mShowTrails && mCfg.trailLength > 0) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 4, -1, "trail_update");
        dispatchTrailUpdate(agentCount);
        glPopDebugGroup();

        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 5, -1, "trail_render");
        renderTrails(agentCount);
        glPopDebugGroup();
        }

        // ── Task 1.7: Cull + Indirect Draw OR plain instanced draw ─────────────
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 2, -1, "agents");
        glEnable(GL_DEPTH_TEST);

        const bool usePoints =
            (cam.mode == CameraMode::Perspective25D) ? false : (cam.zoom < 0.2f);
        const GLuint prog = usePoints ? mPointsProgram : mAgentProgram;
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "uMode"),
                    static_cast<int>(mVizMode));
        glUniform1f(glGetUniformLocation(prog, "uTime"), timeSeconds);

        if (mFrustumCullingEnabled && mCullProgram && !usePoints) {
        // Run culling compute, then draw the compacted visible-agent buffer
        // indirectly.
        dispatchCull(agentCount, cam);

        glUseProgram(prog);

        glUniform1i(glGetUniformLocation(prog, "uCullingEnabled"), 1);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, mVisibleAgentTypeSsbo);

        glBindVertexArray(mCullAgentVao);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, mDrawCmdBuf);
        glDrawArraysIndirect(GL_TRIANGLES, nullptr);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        } else {
        // Fallback: draw all agents (also used for GL_POINTS LOD path).
        glUniform1i(glGetUniformLocation(prog, "uCullingEnabled"), 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mAgentTypeSsbo);

        glBindVertexArray(mAgentVao);
        if (usePoints) {
            glDrawArraysInstanced(GL_POINTS, 0, 1, agentCount);
        } else {
            glDrawArraysInstanced(GL_TRIANGLES, 0, 3, agentCount);
        }
        }
        glPopDebugGroup();

        // ── Task 1.8: Debug Velocity Vectors ────────────────────────────────────
        if (mShowVelocityVectors && mDebugVecProgram) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 6, -1,
                        "debug_velocity_vectors");
        glUseProgram(mDebugVecProgram);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mAgentVbo);
        glBindVertexArray(
            mDummyVao); // required by GL core profile even with SSBO-only shaders

        const float vecScale = 3.0f; // tune: world-units per unit of velocity
        glUniform1f(glGetUniformLocation(mDebugVecProgram, "uVecScale"),
                    vecScale);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // 2 vertices per line (tail + tip), one line per agent instance.
        glDrawArraysInstanced(GL_LINES, 0, 2, agentCount);
        glPopDebugGroup();
        }

        // ── Task 4.6 (Bloom Post-Process) ────────────────────────────────────────
        if (mGlowEnabled && mBloom.sceneFbo) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 7, -1, "bloom_postprocess");
        runBloomPasses();
        glPopDebugGroup();
        }
    }

    void Renderer::render(int agentCount) {
        FrameStats s{};
        s.dtSeconds = 1.0 / 60.0;
        s.frameMsAvg = 16.666;
        s.fpsAvg = 60.0;
        render(agentCount, 0.0f, s);
    }

    // =============================================================================
    // Task 4.5/1.6 — Agent Type Buffer
    // =============================================================================
    bool Renderer::createAgentTypeSsbo(std::string * outError) {
        (void)outError;
        const GLsizeiptr bytes =
            static_cast<GLsizeiptr>(mCfg.maxAgents) * sizeof(uint32_t);
        SWARM_GL_CALL(glCreateBuffers(1, &mAgentTypeSsbo));
        SWARM_GL_CALL(
            glNamedBufferData(mAgentTypeSsbo, bytes, nullptr, GL_DYNAMIC_DRAW));

        // Binding 5: Used by agent.vert
        SWARM_GL_CALL(
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, mAgentTypeSsbo));
        return true;
    }

    void Renderer::uploadAgentTypes(const uint32_t *types, int count) {
        if (!mAgentTypeSsbo || !types || count <= 0)
        return;
        const int safeCount = std::min(count, mCfg.maxAgents);
        SWARM_GL_CALL(glNamedBufferSubData(mAgentTypeSsbo, 0,
                                        safeCount * sizeof(uint32_t), types));
    }

    // =============================================================================
    // Glow / Bloom Pipeline
    // =============================================================================
    bool Renderer::createBloomPipeline(std::string * outError) {
        ShaderProgram p1, p2, p3;
        if (!p1.loadFromFiles("shaders/fullscreen.vert",
                            "shaders/bloom_bright.frag", outError))
        return false;
        mBloomBrightProgram = p1.release();

        if (!p2.loadFromFiles("shaders/fullscreen.vert", "shaders/bloom_blur.frag",
                            outError))
        return false;
        mBloomBlurProgram = p2.release();

        if (!p3.loadFromFiles("shaders/fullscreen.vert",
                            "shaders/bloom_composite.frag", outError))
        return false;
        mBloomCompositeProgram = p3.release();

        return true;
    }

    void Renderer::destroyBloomTargets() {
        if (mBloom.sceneFbo)
        glDeleteFramebuffers(1, &mBloom.sceneFbo);
        if (mBloom.colorTex)
        glDeleteTextures(1, &mBloom.colorTex);
        if (mBloom.depthRbo)
        glDeleteRenderbuffers(1, &mBloom.depthRbo);

        if (mBloom.brightFbo)
        glDeleteFramebuffers(1, &mBloom.brightFbo);
        if (mBloom.brightTex)
        glDeleteTextures(1, &mBloom.brightTex);

        if (mBloom.pingFbo[0])
        glDeleteFramebuffers(2, mBloom.pingFbo);
        if (mBloom.pingTex[0])
        glDeleteTextures(2, mBloom.pingTex);

        mBloom = {};
    }

    void Renderer::rebuildBloomTargets() {
        destroyBloomTargets();

        if (mViewportW <= 0 || mViewportH <= 0)
        return;

        mBloom.width = mViewportW;
        mBloom.height = mViewportH;

        // Scene HDR colour buffer + depth
        glCreateFramebuffers(1, &mBloom.sceneFbo);
        glCreateTextures(GL_TEXTURE_2D, 1, &mBloom.colorTex);
        glTextureStorage2D(mBloom.colorTex, 1, GL_RGBA16F, mViewportW, mViewportH);
        glTextureParameteri(mBloom.colorTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(mBloom.colorTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(mBloom.colorTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(mBloom.colorTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glCreateRenderbuffers(1, &mBloom.depthRbo);
        glNamedRenderbufferStorage(mBloom.depthRbo, GL_DEPTH_COMPONENT32F,
                                mViewportW, mViewportH);

        glNamedFramebufferTexture(mBloom.sceneFbo, GL_COLOR_ATTACHMENT0,
                                mBloom.colorTex, 0);
        glNamedFramebufferRenderbuffer(mBloom.sceneFbo, GL_DEPTH_ATTACHMENT,
                                    GL_RENDERBUFFER, mBloom.depthRbo);

        // Bright-pass FBO (half-res)
        int hw = std::max(1, mViewportW / 2);
        int hh = std::max(1, mViewportH / 2);

        glCreateFramebuffers(1, &mBloom.brightFbo);
        glCreateTextures(GL_TEXTURE_2D, 1, &mBloom.brightTex);
        glTextureStorage2D(mBloom.brightTex, 1, GL_RGBA16F, hw, hh);
        glTextureParameteri(mBloom.brightTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(mBloom.brightTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(mBloom.brightTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(mBloom.brightTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(mBloom.brightFbo, GL_COLOR_ATTACHMENT0,
                                mBloom.brightTex, 0);

        // Ping-pong blur FBOs (half-res)
        glCreateFramebuffers(2, mBloom.pingFbo);
        glCreateTextures(GL_TEXTURE_2D, 2, mBloom.pingTex);
        for (int i = 0; i < 2; ++i) {
        glTextureStorage2D(mBloom.pingTex[i], 1, GL_RGBA16F, hw, hh);
        glTextureParameteri(mBloom.pingTex[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(mBloom.pingTex[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(mBloom.pingTex[i], GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_EDGE);
        glTextureParameteri(mBloom.pingTex[i], GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(mBloom.pingFbo[i], GL_COLOR_ATTACHMENT0,
                                    mBloom.pingTex[i], 0);
        }
    }

    void Renderer::runBloomPasses() {
        int hw = std::max(1, mBloom.width / 2);
        int hh = std::max(1, mBloom.height / 2);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindVertexArray(mDummyVao); // dummy vao for full-screen triangles

        // 1. Bright Pass
        glBindFramebuffer(GL_FRAMEBUFFER, mBloom.brightFbo);
        glViewport(0, 0, hw, hh);
        glUseProgram(mBloomBrightProgram);
        glBindTextureUnit(0, mBloom.colorTex);
        glUniform1i(glGetUniformLocation(mBloomBrightProgram, "uScene"), 0);
        glUniform1f(glGetUniformLocation(mBloomBrightProgram, "uThreshold"), 0.72f);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // 2. Gaussian Blur Ping-Pong
        glUseProgram(mBloomBlurProgram);
        glUniform1i(glGetUniformLocation(mBloomBlurProgram, "uTex"), 0);
        glUniform1f(glGetUniformLocation(mBloomBlurProgram, "uRadius"),
                    1.2f); // tune spread

        bool horizontal = true, first_iteration = true;
        const int amount = 10; // 5 passes each axis

        for (int i = 0; i < amount; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, mBloom.pingFbo[horizontal ? 1 : 0]);
        glUniform2f(glGetUniformLocation(mBloomBlurProgram, "uDir"),
                    horizontal ? 1.0f / hw : 0.0f, horizontal ? 0.0f : 1.0f / hh);

        glBindTextureUnit(0, first_iteration
                                ? mBloom.brightTex
                                : mBloom.pingTex[horizontal ? 0 : 1]);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        horizontal = !horizontal;
        if (first_iteration)
            first_iteration = false;
        }

        // 3. Composite back to Default Framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, mBloom.width, mBloom.height);
        glUseProgram(mBloomCompositeProgram);
        glBindTextureUnit(0, mBloom.colorTex);
        glBindTextureUnit(
            1, mBloom.pingTex[horizontal ? 0 : 1]); // the last buffer written
        glUniform1i(glGetUniformLocation(mBloomCompositeProgram, "uScene"), 0);
        glUniform1i(glGetUniformLocation(mBloomCompositeProgram, "uBloom"), 1);
        glUniform1f(glGetUniformLocation(mBloomCompositeProgram, "uBloomStrength"),
                    0.5f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
} // namespace swarm
