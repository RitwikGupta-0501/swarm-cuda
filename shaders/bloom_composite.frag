#version 460 core

// Bloom pass 4: composite scene + blurred bloom onto the default framebuffer.
// Applies tone-mapping (Reinhard) so HDR values look correct.

in  vec2 vUv;
layout(location = 0) out vec4 oColor;

uniform sampler2D uScene;       // original HDR scene render
uniform sampler2D uBloom;       // blurred bright-pass
uniform float     uBloomStrength; // default 0.45

vec3 reinhardTonemap(vec3 hdr) {
  return hdr / (hdr + vec3(1.0));
}

void main() {
  vec3 scene = texture(uScene, vUv).rgb;
  vec3 bloom = texture(uBloom, vUv).rgb;

  // Additive combination then tonemap.
  vec3 combined = scene + bloom * uBloomStrength;
  vec3 tonemapped = reinhardTonemap(combined);

  // Gamma correction (assume linear framebuffer, output sRGB).
  oColor = vec4(pow(tonemapped, vec3(1.0 / 2.2)), 1.0);
}
