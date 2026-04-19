#version 460 core

in VS_OUT {
    vec2 vel;
    float speed;
    flat uint agentType;
} fIn;

layout(location = 0) out vec4 oColor;

uniform float uTime;
uniform int uMode;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 heat(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 a = vec3(0.1, 0.2, 1.0);
    vec3 b = vec3(0.2, 1.0, 1.0);
    vec3 c = vec3(1.0, 1.0, 0.2);
    vec3 d = vec3(1.0, 0.2, 0.1);
    if (t < 0.33) return mix(a, b, t / 0.33);
    if (t < 0.66) return mix(b, c, (t - 0.33) / 0.33);
    return mix(c, d, (t - 0.66) / 0.34);
}

vec3 typeColor(uint t, float speed) {
    float s = clamp(speed * 0.025, 0.0, 1.0);
    float brt = 0.75 + 0.25 * s;
    if (t == 0u) {
        return hsv2rgb(vec3(0.50 + s * 0.04, 0.80, brt));
    } else if (t == 1u) {
        return hsv2rgb(vec3(0.05 - s * 0.05, 0.90, brt));
    } else {
        return hsv2rgb(vec3(0.83, 0.75, brt));
    }
}

void main() {
    vec3 col = vec3(0.9);
    if (uMode == 1) {
        col = heat(fIn.speed * 15.0);
    } else if (uMode == 2) {
        float ang = atan(fIn.vel.y, fIn.vel.x);
        float h = fract((ang / (2.0 * 3.14159265)) + 1.0);
        col = hsv2rgb(vec3(h, 0.9, 1.0));
    } else if (uMode == 3) {
        float h = fract(uTime * 0.05);
        col = hsv2rgb(vec3(h, 0.85, 1.0));
    } else if (uMode == 4) {
        col = typeColor(fIn.agentType, fIn.speed * 15.0);
    }
    oColor = vec4(col, 0.65);
}
