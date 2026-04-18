#version 460 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

layout(std140, binding = 0) uniform CameraUBO {
  mat4 uView;
  mat4 uProj;
  mat4 uViewProj;
  vec4 uCameraPos_Time;
  vec4 uViewport_Zoom;
};

out vec4 vColor;

void main() {
  vColor = aColor;
  gl_Position = uViewProj * vec4(aPos, 0.0, 1.0);
}

