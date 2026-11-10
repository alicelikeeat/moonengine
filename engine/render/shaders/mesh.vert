#version 450
#extension GL_GOOGLE_include_directive : require
// Scene vertex shader: world-space position and tangent frame for lighting.

#include "common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_tangent;

void main() {
    const vec4 world = pc.model * vec4(in_position, 1.0);
    const mat3 linear = mat3(pc.model);
    v_world = world.xyz;
    v_normal = transpose(inverse(linear)) * in_normal;
    // A mirroring transform flips the bitangent.
    v_tangent = vec4(linear * in_tangent.xyz, in_tangent.w * (determinant(linear) < 0.0 ? -1.0 : 1.0));
    v_uv = in_uv;
    gl_Position = frame.view_proj * world;
}
