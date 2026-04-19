#version 460 core

layout(location = 1) in vec2 iPosition;
layout(location = 2) in vec2 iVelocity;

layout(std140, binding = 0) uniform CameraUBO {
    mat4 uView;
    mat4 uProj;
    mat4 uViewProj;
    vec4 uCameraPos_Time;
    vec4 uViewport_Zoom;
};

layout(std430, binding = 5) readonly buffer AgentTypeBuf {
    uint agentTypes[];
};

uniform float uTime;
uniform int uMode;
uniform float uAgentSize;

out VS_OUT {
    vec2 vel;
    float speed;
    flat uint agentType;
} vOut;

void main() {
    gl_Position = uViewProj * vec4(iPosition, 0.0, 1.0);
    gl_PointSize = uAgentSize;
    vOut.vel = iVelocity;
    vOut.speed = length(iVelocity);
    vOut.agentType = agentTypes[gl_InstanceID];
}
