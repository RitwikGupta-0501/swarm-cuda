#version 460 core

// Trail ring-buffer: [maxAgents][trailLength][vec2]
layout(std430, binding = 4) readonly buffer TrailBuf {
  vec2 positions[];
};

// Compacted visible agent IDs (binding 7)
layout(std430, binding = 7) readonly buffer VisibleAgentIdBuf {
  uint visAgentIds[];
};

layout(std140, binding = 0) uniform CameraUBO {
  mat4 uView;
  mat4 uProj;
  mat4 uViewProj;
  vec4 uCameraPos_Time;
  vec4 uViewport_Zoom;
};

uniform uint uTrailLength;
uniform uint uFrameIdx;     // current head in the ring buffer
uniform bool uCullingEnabled;

out float vAge;  // 0.0 (newest) to 1.0 (oldest)

void main() {
  // gl_VertexID: index within one agent's trail history [0 .. uTrailLength-1]
  // gl_InstanceID: index of the current agent being rendered
  
  uint originalAgentId;
  if (uCullingEnabled) {
    originalAgentId = visAgentIds[gl_InstanceID];
  } else {
    originalAgentId = gl_InstanceID;
  }

  // Calculate the index in the global flattened ring buffer.
  // History is stored in chronologically rotating order.
  // uFrameIdx is the index most recently written to.
  int historyOffset = int(gl_VertexID);
  int accessIdx = (int(uFrameIdx) - historyOffset);
  if (accessIdx < 0) accessIdx += int(uTrailLength);

  uint flatIdx = originalAgentId * uTrailLength + uint(accessIdx);
  vec2 worldPos = positions[flatIdx];

  gl_Position = uViewProj * vec4(worldPos, 0.0, 1.0);
  vAge = float(gl_VertexID) / float(uTrailLength - 1u);
}
