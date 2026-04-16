#include "debug_draw.h"

#include "error.h"
#include "gl_shader.h"

#include <glm/glm.hpp>

#include <array>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace swarm {
namespace {

struct LineVert {
  float x, y;
  float r, g, b, a;
};

struct TextVert {
  float x, y;     // pixel space
  float u, v;     // atlas uv
  float r, g, b, a;
};

// 8x8 glyphs for ASCII 32..126 from a tiny built-in font (subset adapted).
// Each byte is a row, MSB -> left pixel.
// For simplicity, we only define the characters we need; others are blank.
constexpr uint8_t blank8[8] = {0,0,0,0,0,0,0,0};

constexpr uint8_t glyph_F[8] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00};
constexpr uint8_t glyph_P[8] = {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00};
constexpr uint8_t glyph_S[8] = {0x3E,0x60,0x60,0x3C,0x06,0x06,0x7C,0x00};
constexpr uint8_t glyph_m[8] = {0x00,0x00,0x6C,0x7E,0x6A,0x6A,0x6A,0x00};
constexpr uint8_t glyph_s[8] = {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00};
constexpr uint8_t glyph_a[8] = {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00};
constexpr uint8_t glyph_g[8] = {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x7C};
constexpr uint8_t glyph_e[8] = {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00};
constexpr uint8_t glyph_n[8] = {0x00,0x00,0x5C,0x66,0x66,0x66,0x66,0x00};
constexpr uint8_t glyph_t[8] = {0x10,0x10,0x7C,0x10,0x10,0x10,0x0E,0x00};
constexpr uint8_t glyph_c[8] = {0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00};
constexpr uint8_t glyph_o[8] = {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00};
constexpr uint8_t glyph_u[8] = {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00};
constexpr uint8_t glyph_r[8] = {0x00,0x00,0x5C,0x66,0x60,0x60,0x60,0x00};
constexpr uint8_t glyph_D[8] = {0x7C,0x66,0x66,0x66,0x66,0x66,0x7C,0x00};
constexpr uint8_t glyph_colon[8] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00};
constexpr uint8_t glyph_dot[8] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00};
constexpr uint8_t glyph_space[8] = {0,0,0,0,0,0,0,0};

constexpr uint8_t glyph_0[8] = {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00};
constexpr uint8_t glyph_1[8] = {0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00};
constexpr uint8_t glyph_2[8] = {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00};
constexpr uint8_t glyph_3[8] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00};
constexpr uint8_t glyph_4[8] = {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0x00};
constexpr uint8_t glyph_5[8] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00};
constexpr uint8_t glyph_6[8] = {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00};
constexpr uint8_t glyph_7[8] = {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00};
constexpr uint8_t glyph_8[8] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00};
constexpr uint8_t glyph_9[8] = {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00};

const uint8_t* glyphFor(char c) {
  switch (c) {
    case 'F': return glyph_F;
    case 'P': return glyph_P;
    case 'S': return glyph_S;
    case 'm': return glyph_m;
    case 's': return glyph_s;
    case 'a': return glyph_a;
    case 'g': return glyph_g;
    case 'e': return glyph_e;
    case 'n': return glyph_n;
    case 't': return glyph_t;
    case 'c': return glyph_c;
    case 'o': return glyph_o;
    case 'u': return glyph_u;
    case 'r': return glyph_r;
    case 'D': return glyph_D;
    case ':': return glyph_colon;
    case '.': return glyph_dot;
    case ' ': return glyph_space;
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    default: return blank8;
  }
}

} // namespace

DebugDraw::~DebugDraw() {
  if (mLineProg) glDeleteProgram(mLineProg);
  if (mLineVao) glDeleteVertexArrays(1, &mLineVao);
  if (mLineVbo) glDeleteBuffers(1, &mLineVbo);

  if (mTextProg) glDeleteProgram(mTextProg);
  if (mTextVao) glDeleteVertexArrays(1, &mTextVao);
  if (mTextVbo) glDeleteBuffers(1, &mTextVbo);
  if (mFontTex) glDeleteTextures(1, &mFontTex);
}

bool DebugDraw::init(std::string* outError) {
  if (!createLinePipeline(outError)) return false;
  if (!createFontTexture(outError)) return false;
  if (!createFontPipeline(outError)) return false;
  return true;
}

void DebugDraw::resize(int w, int h) {
  mW = std::max(1, w);
  mH = std::max(1, h);
}

bool DebugDraw::createLinePipeline(std::string* outError) {
  ShaderProgram p;
  if (!p.loadFromFiles("shaders/line.vert", "shaders/line.frag", outError)) return false;
  mLineProg = p.release();

  glCreateVertexArrays(1, &mLineVao);
  glCreateBuffers(1, &mLineVbo);
  glNamedBufferData(mLineVbo, 1024 * 1024, nullptr, GL_DYNAMIC_DRAW);

  glVertexArrayVertexBuffer(mLineVao, 0, mLineVbo, 0, sizeof(LineVert));
  glEnableVertexArrayAttrib(mLineVao, 0);
  glVertexArrayAttribFormat(mLineVao, 0, 2, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(mLineVao, 0, 0);
  glEnableVertexArrayAttrib(mLineVao, 1);
  glVertexArrayAttribFormat(mLineVao, 1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 2);
  glVertexArrayAttribBinding(mLineVao, 1, 0);

  return true;
}

bool DebugDraw::createFontTexture(std::string* outError) {
  (void)outError;
  // Atlas: 128 glyphs across, each 8x8. Texture size: 1024 x 8 (R8).
  const int glyphW = 8, glyphH = 8;
  const int cols = 128;
  const int texW = cols * glyphW;
  const int texH = glyphH;
  std::vector<uint8_t> pixels(static_cast<size_t>(texW * texH), 0);

  for (int gi = 0; gi < 128; ++gi) {
    const char c = static_cast<char>(gi);
    const uint8_t* g = glyphFor(c);
    for (int y = 0; y < glyphH; ++y) {
      const uint8_t row = g[y];
      for (int x = 0; x < glyphW; ++x) {
        const bool on = ((row >> (7 - x)) & 1) != 0;
        const int px = gi * glyphW + x;
        const int py = y;
        pixels[static_cast<size_t>(py * texW + px)] = on ? 255 : 0;
      }
    }
  }

  glCreateTextures(GL_TEXTURE_2D, 1, &mFontTex);
  glTextureStorage2D(mFontTex, 1, GL_R8, texW, texH);
  glTextureSubImage2D(mFontTex, 0, 0, 0, texW, texH, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
  glTextureParameteri(mFontTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTextureParameteri(mFontTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTextureParameteri(mFontTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(mFontTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return true;
}

bool DebugDraw::createFontPipeline(std::string* outError) {
  ShaderProgram p;
  if (!p.loadFromFiles("shaders/text.vert", "shaders/text.frag", outError)) return false;
  mTextProg = p.release();

  glCreateVertexArrays(1, &mTextVao);
  glCreateBuffers(1, &mTextVbo);
  glNamedBufferData(mTextVbo, 256 * 1024, nullptr, GL_DYNAMIC_DRAW);

  glVertexArrayVertexBuffer(mTextVao, 0, mTextVbo, 0, sizeof(TextVert));

  glEnableVertexArrayAttrib(mTextVao, 0);
  glVertexArrayAttribFormat(mTextVao, 0, 2, GL_FLOAT, GL_FALSE, 0);
  glVertexArrayAttribBinding(mTextVao, 0, 0);

  glEnableVertexArrayAttrib(mTextVao, 1);
  glVertexArrayAttribFormat(mTextVao, 1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2);
  glVertexArrayAttribBinding(mTextVao, 1, 0);

  glEnableVertexArrayAttrib(mTextVao, 2);
  glVertexArrayAttribFormat(mTextVao, 2, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 4);
  glVertexArrayAttribBinding(mTextVao, 2, 0);

  return true;
}

void DebugDraw::drawGridAndAxes(const CameraMatrices& cam) {
  std::vector<LineVert> lines;
  lines.reserve(8192);

  const float zoom = std::max(0.0001f, cam.zoom);
  const float gridStepBase = 80.0f;
  float step = gridStepBase;
  if (zoom > 5.0f) step *= 0.5f;
  if (zoom > 20.0f) step *= 0.5f;
  if (zoom < 0.5f) step *= 2.0f;
  if (zoom < 0.2f) step *= 2.0f;

  // Draw world-space grid lines in a fixed range around origin (cheap + stable).
  const int count = 120;
  const float half = step * count * 0.5f;
  const float a = 0.12f;
  for (int i = 0; i <= count; ++i) {
    const float x = -half + i * step;
    lines.push_back({x, -half, 0.45f, 0.52f, 0.62f, a});
    lines.push_back({x, half, 0.45f, 0.52f, 0.62f, a});

    const float y = -half + i * step;
    lines.push_back({-half, y, 0.45f, 0.52f, 0.62f, a});
    lines.push_back({half, y, 0.45f, 0.52f, 0.62f, a});
  }

  // Axes (X red, Y green).
  lines.push_back({-half, 0.0f, 1.0f, 0.2f, 0.2f, 0.6f});
  lines.push_back({half, 0.0f, 1.0f, 0.2f, 0.2f, 0.6f});
  lines.push_back({0.0f, -half, 0.2f, 1.0f, 0.2f, 0.6f});
  lines.push_back({0.0f, half, 0.2f, 1.0f, 0.2f, 0.6f});

  glUseProgram(mLineProg);
  glBindVertexArray(mLineVao);

  // Lines use Camera UBO already bound at binding=0.
  glNamedBufferSubData(mLineVbo, 0, static_cast<GLsizeiptr>(lines.size() * sizeof(LineVert)),
                       lines.data());

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
}

void DebugDraw::drawOverlayText(const FrameStats& stats, int agentCount, int vizMode, int cameraMode) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "FPS:%0.1f ms:%0.2f agents:%d mode:%d cam:%d",
                stats.fpsAvg, stats.frameMsAvg, agentCount, vizMode, cameraMode);

  const std::string text(buf);
  const float x = 10.0f;
  const float y = 10.0f;
  const float scale = 2.0f;

  std::vector<TextVert> verts;
  verts.reserve(text.size() * 6);

  // Build quads in pixel space (top-left origin).
  const int glyphW = 8;
  const int glyphH = 8;
  const float sx = static_cast<float>(glyphW) * scale;
  const float sy = static_cast<float>(glyphH) * scale;
  float penX = x;
  float penY = y;

  const float texW = 128.0f * glyphW;
  const float texH = static_cast<float>(glyphH);

  for (char c : text) {
    if (c == '\n') {
      penX = x;
      penY += sy + 2.0f;
      continue;
    }
    const int gi = (static_cast<unsigned char>(c) < 128) ? static_cast<unsigned char>(c) : 0;
    const float u0 = (gi * glyphW) / texW;
    const float v0 = 0.0f;
    const float u1 = ((gi + 1) * glyphW) / texW;
    const float v1 = 1.0f;

    const float x0 = penX;
    const float y0 = penY;
    const float x1 = penX + sx;
    const float y1 = penY + sy;

    const float r = 0.92f, g = 0.95f, b = 1.0f, a = 0.85f;

    verts.push_back({x0, y0, u0, v0, r, g, b, a});
    verts.push_back({x1, y0, u1, v0, r, g, b, a});
    verts.push_back({x1, y1, u1, v1, r, g, b, a});
    verts.push_back({x0, y0, u0, v0, r, g, b, a});
    verts.push_back({x1, y1, u1, v1, r, g, b, a});
    verts.push_back({x0, y1, u0, v1, r, g, b, a});

    penX += sx * 0.75f;
  }

  glUseProgram(mTextProg);
  glBindVertexArray(mTextVao);
  glBindTextureUnit(0, mFontTex);
  glUniform1i(glGetUniformLocation(mTextProg, "uFont"), 0);
  glUniform2f(glGetUniformLocation(mTextProg, "uViewport"), static_cast<float>(mW),
              static_cast<float>(mH));

  glNamedBufferSubData(mTextVbo, 0, static_cast<GLsizeiptr>(verts.size() * sizeof(TextVert)),
                       verts.data());

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
}

} // namespace swarm

