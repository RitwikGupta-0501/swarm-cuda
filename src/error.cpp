#include "error.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>

#ifndef SWARM_RUNTIME_VALIDATION
#define SWARM_RUNTIME_VALIDATION 0
#endif

namespace swarm {
namespace {

std::mutex gLogMutex;

void glfwErrorCb(int code, const char* desc) {
  std::scoped_lock lk(gLogMutex);
  std::cerr << "[GLFW] (" << code << ") " << (desc ? desc : "") << "\n";
}

void APIENTRY glDebugCb(GLenum source, GLenum type, GLuint id, GLenum severity,
                        GLsizei /*length*/, const GLchar* message,
                        const void* /*userParam*/) {
  if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

  std::scoped_lock lk(gLogMutex);
  std::cerr << "[GL] severity=" << severity << " id=" << id << " msg=" << (message ? message : "")
            << "\n";
}

} // namespace

[[noreturn]] void fatal(const char* file, int line, const std::string& msg) {
  std::scoped_lock lk(gLogMutex);
  std::cerr << "[FATAL] " << file << ":" << line << " " << msg << "\n";
  std::abort();
}

void logInfo(const std::string& msg) {
  std::scoped_lock lk(gLogMutex);
  std::cerr << "[INFO] " << msg << "\n";
}
void logWarn(const std::string& msg) {
  std::scoped_lock lk(gLogMutex);
  std::cerr << "[WARN] " << msg << "\n";
}
void logError(const std::string& msg) {
  std::scoped_lock lk(gLogMutex);
  std::cerr << "[ERROR] " << msg << "\n";
}

void installGlfwErrorCallback() {
  glfwSetErrorCallback(glfwErrorCb);
}

void installGlDebugCallbackIfAvailable() {
  // Requires a current context and GLAD loaded.
  if (glDebugMessageCallback) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugCb, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr,
                          GL_FALSE);
  }
}

bool runtimeValidationEnabled() {
#if SWARM_RUNTIME_VALIDATION
  return true;
#else
  return false;
#endif
}

void clearGlErrors() {
  while (glGetError() != GL_NO_ERROR) {
  }
}

bool checkGlErrors(const char* file, int line, const char* expr) {
  bool ok = true;
  GLenum err = GL_NO_ERROR;
  while ((err = glGetError()) != GL_NO_ERROR) {
    ok = false;
    std::ostringstream ss;
    ss << "GL error 0x" << std::hex << static_cast<unsigned int>(err) << std::dec << " after "
       << (expr ? expr : "<expr>") << " at " << file << ":" << line;
    logError(ss.str());
  }
  return ok;
}

} // namespace swarm

