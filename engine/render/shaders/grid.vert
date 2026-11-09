#version 450
#extension GL_GOOGLE_include_directive : require
// Editor ground grid: a large quad on the XZ plane; the fragment shader
// draws the lines.

#include "common.glsl"

layout(location = 0) in vec3 in_position;

layout(location = 0) out vec3 v_world;

void main() {
    v_world = in_position;
    gl_Position = frame.view_proj * vec4(in_position, 1.0);
}
