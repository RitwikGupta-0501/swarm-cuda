#version 460 core

in  float vAge;                              // 0 = newest, 1 = oldest
layout(location = 0) out vec4 oColor;

uniform int uMode;  // matches agent VizMode for consistent colour palette

void main() {
  // Fade out toward the tail: alpha and brightness both drop.
  float a     = pow(1.0 - vAge, 1.8);       // nonlinear fade
  float bright = mix(0.9, 0.2, vAge);

  // Trail colour follows the same mode as agents (uniform = cyan-ish, else warm).
  vec3 col;
  if (uMode == 0) {
    col = vec3(0.3, 0.8, 1.0) * bright;     // Uniform: icy blue trail
  } else if (uMode == 1) {
    // VelocityHeat: tail fades cool → warm handled in vertex; here just desaturate.
    col = vec3(1.0, 0.6, 0.1) * bright;
  } else if (uMode == 2) {
    col = vec3(0.6, 1.0, 0.5) * bright;     // Direction: soft green
  } else {
    col = vec3(1.0, 0.8, 0.3) * bright;     // Rainbow: warm gold
  }

  oColor = vec4(col, a * 0.6);              // max alpha 0.6 to not overpower agents
}
