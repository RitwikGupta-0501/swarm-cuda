#include "camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace swarm {
namespace {

float expSmoothing(float current, float target, float sharpness, float dt) {
  // sharpness: higher = snappier. Stable across frame rates.
  const float a = 1.0f - std::exp(-sharpness * dt);
  return current + (target - current) * a;
}

glm::vec2 expSmoothing(glm::vec2 current, glm::vec2 target, float sharpness, float dt) {
  const float a = 1.0f - std::exp(-sharpness * dt);
  return current + (target - current) * a;
}

glm::vec3 expSmoothing(glm::vec3 current, glm::vec3 target, float sharpness, float dt) {
  const float a = 1.0f - std::exp(-sharpness * dt);
  return current + (target - current) * a;
}


} // namespace

void Camera::setViewport(int w, int h) {
  mW = std::max(1, w);
  mH = std::max(1, h);
}

void Camera::setMode(CameraMode m) {
  mMode = m;
}

void Camera::onMouseButton(int button, int action, int /*mods*/) {
  // RMB drag pans in 2D; in 2.5D drag orbits.
  if (button == 1 /*GLFW_MOUSE_BUTTON_RIGHT*/) {
    mDragging = (action == 1 /*GLFW_PRESS*/);
  }
}

void Camera::onMouseMove(double x, double y) {
  if (!mDragging) {
    mLastMouseX = x;
    mLastMouseY = y;
    return;
  }

  const double dx = x - mLastMouseX;
  const double dy = y - mLastMouseY;
  mLastMouseX = x;
  mLastMouseY = y;

  if (mMode == CameraMode::Ortho2D) {
      // Calculate how many world units 1 pixel represents
      // worldHeight is 2.0 / zoom. Divide by screen height (mH) to get units per pixel.
      const float unitsPerPixel = 2.0f / (static_cast<float>(mH) * std::max(0.0001f, m2d.targetZoom));

      // Multiply the pixel movement by unitsPerPixel to get smooth, 1-to-1 dragging
      m2d.targetPosition += glm::vec2(static_cast<float>(-dx), static_cast<float>(dy)) * unitsPerPixel;
    } else {
    // Orbit.
    m25d.targetYaw += static_cast<float>(dx) * 0.006f;
    m25d.targetPitch += static_cast<float>(dy) * 0.006f;
    m25d.targetPitch = std::clamp(m25d.targetPitch, 0.15f, 1.45f);
  }
}

glm::vec2 Camera::screenToWorld2D(double sx, double sy) const {
  // Screen (pixels) -> world, using target camera state to keep zoom-to-cursor stable.
  const float w = static_cast<float>(mW);
  const float h = static_cast<float>(mH);

  const float nx = static_cast<float>(sx) - w * 0.5f;
  const float ny = (h * 0.5f) - static_cast<float>(sy);

  const float invZ = 1.0f / std::max(0.0001f, m2d.targetZoom);
  glm::vec2 p(nx * invZ, ny * invZ);

  // Apply inverse rotation.
  const float c = std::cos(-m2d.targetRotation);
  const float s = std::sin(-m2d.targetRotation);
  p = glm::vec2(c * p.x - s * p.y, s * p.x + c * p.y);

  p += m2d.targetPosition;
  return p;
}

void Camera::onScroll(double yOffset, double cursorX, double cursorY) {
    if (mMode == CameraMode::Ortho2D) {
        const glm::vec2 worldBefore = screenToWorld2D(cursorX, cursorY);
        const float zoomFactor = std::pow(1.1f, static_cast<float>(yOffset));
        m2d.targetZoom = std::clamp(m2d.targetZoom * zoomFactor, 0.1f, 10.0f);
        const glm::vec2 worldAfter = screenToWorld2D(cursorX, cursorY);
    } else {
    const float zoomFactor = std::pow(1.1f, static_cast<float>(-yOffset));
    m25d.targetDistance = std::clamp(m25d.targetDistance * zoomFactor, 2.0f, 2000.0f);
  }
}

void Camera::onKey(int key, int /*scancode*/, int action, int /*mods*/) {
  const bool down = (action != 0 /*GLFW_RELEASE*/);
  switch (key) {
    case 'W': mKeyW = down; break;
    case 'A': mKeyA = down; break;
    case 'S': mKeyS = down; break;
    case 'D': mKeyD = down; break;
    case 'Q': mKeyQ = down; break;
    case 'E': mKeyE = down; break;
    default: break;
  }
}

void Camera::update(float dt) {
  const float moveSpeed = 2.0f; // pixels/sec at zoom=1 (2D).

  if (mMode == CameraMode::Ortho2D) {
    glm::vec2 dir(0.0f);
    if (mKeyW) dir.y += 1.0f;
    if (mKeyS) dir.y -= 1.0f;
    if (mKeyD) dir.x += 1.0f;
    if (mKeyA) dir.x -= 1.0f;
    if (glm::dot(dir, dir) > 0.0f) dir = glm::normalize(dir);

    const float invZ = 1.0f / std::max(0.0001f, m2d.targetZoom);
    // Move in camera-rotated space.
    const float c = std::cos(m2d.targetRotation);
    const float s = std::sin(m2d.targetRotation);
    glm::vec2 worldDir(c * dir.x - s * dir.y, s * dir.x + c * dir.y);
    m2d.targetPosition += worldDir * (moveSpeed * invZ * dt);

    if (mKeyQ) m2d.targetRotation += 1.6f * dt;
    if (mKeyE) m2d.targetRotation -= 1.6f * dt;

    // Smooth actual state toward targets.
    m2d.position = expSmoothing(m2d.position, m2d.targetPosition, 14.0f, dt);
    m2d.zoom = expSmoothing(m2d.zoom, m2d.targetZoom, 18.0f, dt);
    m2d.rotation = expSmoothing(m2d.rotation, m2d.targetRotation, 16.0f, dt);
  } else {
    // Smooth orbit parameters.
    m25d.focus = expSmoothing(m25d.focus, m25d.targetFocus, 10.0f, dt);
    m25d.yaw = expSmoothing(m25d.yaw, m25d.targetYaw, 12.0f, dt);
    m25d.pitch = expSmoothing(m25d.pitch, m25d.targetPitch, 12.0f, dt);
    m25d.distance = expSmoothing(m25d.distance, m25d.targetDistance, 14.0f, dt);
  }
}

CameraMatrices Camera::matrices(float timeSeconds) const {
  CameraMatrices m{};
  m.mode = mMode;

  if (mMode == CameraMode::Ortho2D) {
    const float w = static_cast<float>(mW);
    const float h = static_cast<float>(mH);
    const float aspect = w / h;
    const float worldHeight = 2.0f / std::max(0.0001f, m2d.zoom);  // zoom 1.0 = 2 unit height
    const float worldWidth = worldHeight * aspect;
    const float halfW = 0.5f * worldWidth;
    const float halfH = 0.5f * worldHeight;

    m.proj = glm::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);

    glm::mat4 v(1.0f);
    v = glm::rotate(v, -m2d.rotation, glm::vec3(0, 0, 1));
    v = glm::translate(v, glm::vec3(-m2d.position, 0.0f));
    m.view = v;
    m.viewProj = m.proj * m.view;
    m.cameraPos = glm::vec3(m2d.position, 0.0f);
    m.zoom = m2d.zoom;
    (void)timeSeconds;
  } else {
    const float aspect = static_cast<float>(mW) / static_cast<float>(mH);
    m.proj = glm::perspective(glm::radians(55.0f), aspect, 0.1f, 5000.0f);

    const float cy = std::cos(m25d.yaw);
    const float sy = std::sin(m25d.yaw);
    const float cp = std::cos(m25d.pitch);
    const float sp = std::sin(m25d.pitch);
    const glm::vec3 dir = glm::normalize(glm::vec3(cy * cp, sy * cp, sp));
    const glm::vec3 eye = m25d.focus - dir * m25d.distance + glm::vec3(0.0f, 0.0f, 18.0f);

    m.view = glm::lookAt(eye, m25d.focus, glm::vec3(0, 0, 1));
    m.viewProj = m.proj * m.view;
    m.cameraPos = eye;
    m.zoom = 1.0f;
    (void)timeSeconds;
  }
  return m;
}

} // namespace swarm
