#version 460 core

layout(location = 0) in vec2 aBase;
layout(location = 1) in vec2 iPosition;
layout(location = 2) in vec2 iVelocity;

layout(std140, binding = 0) uniform CameraUBO {
  mat4 uView;
  mat4 uProj;
  mat4 uViewProj;
  vec4 uCameraPos_Time;
  vec4 uViewport_Zoom;  // .xy = viewport, .z = zoom, .w = CameraMode (0=Ortho2D, 1=Perspective25D)
};

// Original agent-type SSBO (binding 5)
layout(std430, binding = 5) readonly buffer AgentTypeBuf {
  uint agentTypes[];
};

// Compacted visible agent-type SSBO (binding 6)
layout(std430, binding = 6) readonly buffer VisibleAgentTypeBuf {
  uint visAgentTypes[];
};

uniform float uTime;
uniform int   uMode;
uniform bool  uCullingEnabled;

out VS_OUT {
  vec2 vel;
  float speed;
  flat uint agentType;
} vOut;

mat2 rot2(float a) {
  float c = cos(a), s = sin(a);
  return mat2(c, -s, s, c);
}

void main() {
  float speed = length(iVelocity);
  float ang   = (speed > 1e-6) ? atan(iVelocity.y, iVelocity.x) : 0.0;

  int camMode = int(uViewport_Zoom.w);

  vec4 clipPos;

  if (camMode == 1) {
    // 2.5D Billboarding
    vec3 camRight = vec3(uView[0][0], uView[1][0], uView[2][0]);
    vec3 camUp    = vec3(uView[0][1], uView[1][1], uView[2][1]);

    float velScreenX = dot(vec3(iVelocity, 0.0), camRight);
    float velScreenY = dot(vec3(iVelocity, 0.0), camUp);
    float screenAng  = (speed > 1e-6) ? atan(velScreenY, velScreenX) : 0.0;

    vec2 rotBase = rot2(screenAng) * aBase;
    vec3 worldPos = vec3(iPosition, 0.0)
                  + camRight * rotBase.x
                  + camUp    * rotBase.y;

    clipPos = uViewProj * vec4(worldPos, 1.0);
  } else {
    // 2D Ortho
    vec2 p = iPosition + rot2(ang) * aBase;
    clipPos = uViewProj * vec4(p, 0.0, 1.0);
  }

  gl_Position    = clipPos;
  vOut.vel       = iVelocity;
  vOut.speed     = speed;
  
  // If culling is enabled, gl_InstanceID indexes the compacted visAgentTypes buffer.
  // Otherwise, it indexes the original agentTypes buffer.
  if (uCullingEnabled) {
    vOut.agentType = visAgentTypes[gl_InstanceID];
  } else {
    vOut.agentType = agentTypes[gl_InstanceID];
  }
}
