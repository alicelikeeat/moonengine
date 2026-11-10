#version 450
#extension GL_GOOGLE_include_directive : require
// Depth-only pass into one cascade of the sun's shadow map.

#include "common.glsl"

layout(location = 0) in vec3 in_position;

void main() {
    gl_Position = frame.light_view_proj[pc.material.x] * pc.model * vec4(in_position, 1.0);
}
