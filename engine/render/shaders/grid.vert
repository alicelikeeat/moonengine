#version 450
// Editor ground grid: a large quad on the XZ plane; the fragment shader
// draws the lines. Uses the mesh push-constant layout (128 bytes).

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;  // unused, shares the mesh vertex layout

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera_position;  // xyz
    vec4 unused1;
    vec4 unused2;
    vec4 unused3;
} pc;

layout(location = 0) out vec3 v_world;

void main() {
    v_world = in_position;
    gl_Position = pc.view_proj * vec4(in_position, 1.0);
}
