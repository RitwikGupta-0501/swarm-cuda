#pragma once

#include <glm/glm.hpp>

namespace swarm {

enum class CameraMode : int {
  Ortho2D = 0,
  Perspective25D = 1,
};

struct CameraState2D {
  glm::vec2 position{0.0f, 0.0f};
  float zoom = 1.0f;
  float rotation = 0.0f;

  glm::vec2 targetPosition{0.0f, 0.0f};
  float targetZoom = 1.0f;
  float targetRotation = 0.0f;
};

struct CameraState25D {
  glm::vec3 focus{0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  float pitch = 0.85f;
  float distance = 25.0f;

  glm::vec3 targetFocus{0.0f, 0.0f, 0.0f};
  float targetYaw = 0.0f;
  float targetPitch = 0.85f;
  float targetDistance = 25.0f;
};

struct CameraMatrices {
  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
  glm::mat4 viewProj{1.0f};
  glm::vec3 cameraPos{0.0f, 0.0f, 0.0f};
  float zoom = 1.0f;
  CameraMode mode = CameraMode::Ortho2D;
};

class Camera {
public:
  void setViewport(int w, int h);
  void setFov(float fov) { mFov = fov; }
  void setMode(CameraMode m);
  CameraMode mode() const { return mMode; }

  // Input controls (call from callbacks / main loop).
  void onMouseButton(int button, int action, int mods);
  void onMouseMove(double x, double y);
  void onScroll(double yOffset, double cursorX, double cursorY);
  void onKey(int key, int scancode, int action, int mods);

  // Frame update.
  void update(float dtSeconds);

  CameraMatrices matrices(float timeSeconds) const;

private:
  int mW = 1280;
  int mH = 720;
  float mFov = 60.0f;
  CameraMode mMode = CameraMode::Ortho2D;

  CameraState2D m2d{};
  CameraState25D m25d{};

  bool mDragging = false;
  double mLastMouseX = 0.0;
  double mLastMouseY = 0.0;

  // Key state (WASD/QE).
  bool mKeyW = false, mKeyA = false, mKeyS = false, mKeyD = false;
  bool mKeyQ = false, mKeyE = false;

  glm::vec2 screenToWorld2D(double sx, double sy) const;
};

} // namespace swarm
