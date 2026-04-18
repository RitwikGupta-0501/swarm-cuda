#version 460 core

// Bloom pass 2/3: separable 9-tap Gaussian blur.
// Run twice: once horizontal (uDir = (1,0)), once vertical (uDir = (0,1)).

in  vec2 vUv;
layout(location = 0) out vec4 oColor;

uniform sampler2D uTex;
uniform vec2      uDir;       // (texelW, 0) or (0, texelH)
uniform float     uRadius;    // multiplier, default 1.0; increase for wider glow

// Gaussian weights for offsets -4..+4 (normalised to sum = 1).
const float W[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);

void main() {
  vec3 result = vec3(0.0);
  // 5-tap separable: offsets -2,-1,0,+1,+2 texels * radius.
  for (int i = 0; i < 5; ++i) {
    float offset = (float(i) - 2.0) * uRadius;
    result += W[i] * texture(uTex, vUv + uDir * offset).rgb;
  }
  oColor = vec4(result, 1.0);
}
