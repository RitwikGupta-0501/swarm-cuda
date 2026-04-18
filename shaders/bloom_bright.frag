#version 460 core

// Bloom pass 1: extract pixels brighter than uThreshold.
// Uses luminance-weighted extraction to preserve colour hue.

in  vec2 vUv;
layout(location = 0) out vec4 oColor;

uniform sampler2D uScene;
uniform float uThreshold;  // default ~0.72; agent arrows write ~0.85 alpha so they bloom

void main() {
  vec3 col        = texture(uScene, vUv).rgb;
  float luminance = dot(col, vec3(0.2126, 0.7152, 0.0722));

  // Soft knee: smoothstep instead of hard clip avoids ringing.
  float knee  = uThreshold * 0.5;
  float ramp  = smoothstep(uThreshold - knee, uThreshold + knee, luminance);

  oColor = vec4(col * ramp, 1.0);
}
