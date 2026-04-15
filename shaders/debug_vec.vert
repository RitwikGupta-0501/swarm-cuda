#version 460 core

// Instanced velocity-vector debug lines.
// gl_VertexID 0 = tail (agent position), 1 = tip (position + velocity * scale).
// gl_InstanceID selects the agent from the shared VBO via an SSBO.

struct Agent {
  float position_x;
  float position_y;
  float velocity_x;
  float velocity_y;
};

layout(std430, binding = 1) readonly buffer AgentSSBO {
  Agent agents[];
};

layout(std140, binding = 0) uniform CameraUBO {
  mat4 uView;
  mat4 uProj;
  mat4 uViewProj;
  vec4 uCameraPos_Time;
  vec4 uViewport_Zoom;  // .z = zoom
};

uniform float uVecScale;   // world-space scale for velocity vector length

out vec4 vColor;

void main() {
  Agent a = agents[gl_InstanceID];

  vec2 pos = vec2(a.position_x, a.position_y);
  vec2 vel = vec2(a.velocity_x, a.velocity_y);

  vec2 worldPos;
  float alpha;
  if (gl_VertexID == 0) {
    // Tail: agent position — dim
    worldPos = pos;
    alpha    = 0.3;
  } else {
    // Tip: offset by velocity * scale
    worldPos = pos + vel * uVecScale;
    alpha    = 0.9;
  }

  // Color: yellow->red based on speed
  float speed = length(vel);
  float t     = clamp(speed * 0.02, 0.0, 1.0);
  vec3  col   = mix(vec3(1.0, 0.92, 0.2), vec3(1.0, 0.2, 0.1), t);
  vColor      = vec4(col, alpha);

  gl_Position = uViewProj * vec4(worldPos, 0.0, 1.0);
}
