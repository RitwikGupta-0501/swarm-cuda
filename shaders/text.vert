#version 460 core

layout(location = 0) in vec2 aPosPx;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;

uniform vec2 uViewport; // pixels

out vec2 vUv;
out vec4 vColor;

void main() {
  // Convert from top-left pixel coords to NDC.
  vec2 p = aPosPx / uViewport;
  p.y = 1.0 - p.y;
  vec2 ndc = p * 2.0 - 1.0;
  gl_Position = vec4(ndc, 0.0, 1.0);
  vUv = aUv;
  vColor = aColor;
}

