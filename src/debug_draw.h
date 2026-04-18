#pragma once

#include "camera.h"
#include "time_stats.h"

#include <glad/glad.h>

#include <string>

namespace swarm {

class DebugDraw {
public:
  DebugDraw() = default;
  ~DebugDraw();

  bool init(std::string* outError);
  void resize(int w, int h);

  void drawGridAndAxes(const CameraMatrices& cam);
  void drawOverlayText(const FrameStats& stats, int agentCount, int vizMode, int cameraMode);

private:
  int mW = 1;
  int mH = 1;

  // Simple line renderer (grid/axes).
  GLuint mLineVao = 0;
  GLuint mLineVbo = 0;
  GLuint mLineProg = 0;

  // Bitmap font renderer for overlay.
  GLuint mFontTex = 0;
  GLuint mTextVao = 0;
  GLuint mTextVbo = 0;
  GLuint mTextProg = 0;

  bool createLinePipeline(std::string* outError);
  bool createFontPipeline(std::string* outError);
  bool createFontTexture(std::string* outError);
};

} // namespace swarm

