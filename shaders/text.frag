#version 460 core

in vec2 vUv;
in vec4 vColor;

layout(location = 0) out vec4 oColor;

uniform sampler2D uFont;

void main() {
  float a = texture(uFont, vUv).r;
  oColor = vec4(vColor.rgb, vColor.a * a);
}

