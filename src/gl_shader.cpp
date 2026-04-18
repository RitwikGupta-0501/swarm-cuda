#include "gl_shader.h"

#include <fstream>
#include <sstream>

namespace swarm {
namespace {

std::string readTextFile(const std::string& path) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool compileStage(GLenum type, const std::string& src, GLuint* outShader, std::string* outLog) {
  *outShader = glCreateShader(type);
  const char* c = src.c_str();
  glShaderSource(*outShader, 1, &c, nullptr);
  glCompileShader(*outShader);

  GLint ok = 0;
  glGetShaderiv(*outShader, GL_COMPILE_STATUS, &ok);
  GLint logLen = 0;
  glGetShaderiv(*outShader, GL_INFO_LOG_LENGTH, &logLen);
  if (logLen > 1) {
    std::string log(static_cast<size_t>(logLen), '\0');
    glGetShaderInfoLog(*outShader, logLen, nullptr, log.data());
    if (outLog) *outLog += log;
  }
  return ok == GL_TRUE;
}

} // namespace

ShaderProgram::~ShaderProgram() {
  if (mProgram) glDeleteProgram(mProgram);
}

GLuint ShaderProgram::release() {
  const GLuint p = mProgram;
  mProgram = 0;
  mUniformLoc.clear();
  return p;
}

ShaderProgram::ShaderProgram(ShaderProgram&& o) noexcept {
  mProgram = o.mProgram;
  mUniformLoc = std::move(o.mUniformLoc);
  o.mProgram = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& o) noexcept {
  if (this == &o) return *this;
  if (mProgram) glDeleteProgram(mProgram);
  mProgram = o.mProgram;
  mUniformLoc = std::move(o.mUniformLoc);
  o.mProgram = 0;
  return *this;
}

bool ShaderProgram::loadFromFiles(const std::string& vsPath, const std::string& fsPath,
                                  std::string* outError) {
  const std::string vs = readTextFile(vsPath);
  const std::string fs = readTextFile(fsPath);
  if (vs.empty() || fs.empty()) {
    if (outError) {
      *outError = "Failed to read shader files: " + vsPath + " or " + fsPath;
    }
    return false;
  }

  GLuint vsId = 0, fsId = 0;
  std::string log;
  if (!compileStage(GL_VERTEX_SHADER, vs, &vsId, &log)) {
    if (outError) *outError = "Vertex shader compile failed (" + vsPath + "):\n" + log;
    glDeleteShader(vsId);
    return false;
  }
  log.clear();
  if (!compileStage(GL_FRAGMENT_SHADER, fs, &fsId, &log)) {
    if (outError) *outError = "Fragment shader compile failed (" + fsPath + "):\n" + log;
    glDeleteShader(vsId);
    glDeleteShader(fsId);
    return false;
  }

  GLuint prog = glCreateProgram();
  glAttachShader(prog, vsId);
  glAttachShader(prog, fsId);
  glLinkProgram(prog);

  glDeleteShader(vsId);
  glDeleteShader(fsId);

  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  GLint logLen = 0;
  glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
  if (logLen > 1) {
    std::string plog(static_cast<size_t>(logLen), '\0');
    glGetProgramInfoLog(prog, logLen, nullptr, plog.data());
    if (outError) *outError += plog;
  }

  if (ok != GL_TRUE) {
    if (outError && outError->empty()) *outError = "Program link failed.";
    glDeleteProgram(prog);
    return false;
  }

  if (mProgram) glDeleteProgram(mProgram);
  mProgram = prog;
  mUniformLoc.clear();
  return true;
}

bool ShaderProgram::loadCompute(const std::string& csPath, std::string* outError) {
  const std::string cs = readTextFile(csPath);
  if (cs.empty()) {
    if (outError) *outError = "Failed to read compute shader: " + csPath;
    return false;
  }

  GLuint csId = 0;
  std::string log;
  if (!compileStage(GL_COMPUTE_SHADER, cs, &csId, &log)) {
    if (outError) *outError = "Compute shader compile failed (" + csPath + "):\n" + log;
    glDeleteShader(csId);
    return false;
  }

  GLuint prog = glCreateProgram();
  glAttachShader(prog, csId);
  glLinkProgram(prog);
  glDeleteShader(csId);

  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  GLint logLen = 0;
  glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
  if (logLen > 1) {
    std::string plog(static_cast<size_t>(logLen), '\0');
    glGetProgramInfoLog(prog, logLen, nullptr, plog.data());
    if (outError) *outError += plog;
  }
  if (ok != GL_TRUE) {
    if (outError && outError->empty()) *outError = "Compute program link failed.";
    glDeleteProgram(prog);
    return false;
  }

  if (mProgram) glDeleteProgram(mProgram);
  mProgram = prog;
  mUniformLoc.clear();
  return true;
}

void ShaderProgram::use() const {
  glUseProgram(mProgram);
}

GLint ShaderProgram::uniformLoc(const char* name) {
  auto it = mUniformLoc.find(name);
  if (it != mUniformLoc.end()) return it->second;
  const GLint loc = glGetUniformLocation(mProgram, name);
  mUniformLoc.emplace(name, loc);
  return loc;
}

void ShaderProgram::setInt(const char* name, int v) {
  glUniform1i(uniformLoc(name), v);
}

void ShaderProgram::setFloat(const char* name, float v) {
  glUniform1f(uniformLoc(name), v);
}

void ShaderProgram::setVec4(const char* name, float x, float y, float z, float w) {
  glUniform4f(uniformLoc(name), x, y, z, w);
}

} // namespace swarm

