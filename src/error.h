#pragma once

#include <string>

namespace swarm {

[[noreturn]] void fatal(const char* file, int line, const std::string& msg);
void logInfo(const std::string& msg);
void logWarn(const std::string& msg);
void logError(const std::string& msg);

// OpenGL / GLFW diagnostics.
void installGlfwErrorCallback();
void installGlDebugCallbackIfAvailable();
bool runtimeValidationEnabled();
void clearGlErrors();
bool checkGlErrors(const char* file, int line, const char* expr);

} // namespace swarm

#define SWARM_FATAL(MSG) ::swarm::fatal(__FILE__, __LINE__, (MSG))
#define SWARM_GL_CALL(EXPR)                                                    \
  do {                                                                         \
    if (::swarm::runtimeValidationEnabled()) ::swarm::clearGlErrors();         \
    (EXPR);                                                                    \
    if (::swarm::runtimeValidationEnabled()) {                                 \
      (void)::swarm::checkGlErrors(__FILE__, __LINE__, #EXPR);                \
    }                                                                          \
  } while (0)

