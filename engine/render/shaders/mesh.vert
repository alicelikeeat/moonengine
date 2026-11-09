#version 450
// Mesh vertex shader. Push constants must match PushConstants in
// engine/render/src/vulkan_renderer.cpp (128 bytes, the guaranteed minimum).

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 normal0;  // normal matrix columns (xyz)
    vec4 normal1;
    vec4 normal2;
    vec4 color;    // linear RGB
} pc;

layout(location = 0) out vec3 v_normal;

void main() {
    gl_Position = pc.mvp * vec4(in_position, 1.0);
    v_normal = mat3(pc.normal0.xyz, pc.normal1.xyz, pc.normal2.xyz) * in_normal;
}
