// Shared declarations for the scene shaders. Must match FrameUniforms and
// PushConstants in engine/render/src/vulkan_renderer.cpp.

struct PointLight {
    vec4 position_range;  // xyz: world position, w: range
    vec4 color;           // rgb: colour * intensity
};

layout(set = 0, binding = 1, std140) uniform Frame {
    mat4 view_proj;
    mat4 inv_view_proj;       // clip -> world, for the sky
    mat4 light_view_proj[3];  // world -> shadow clip space, one per cascade
    vec4 camera_position;     // xyz; w: exposure
    vec4 camera_forward;      // xyz: unit view direction
    vec4 sun_direction;       // xyz: unit vector toward the sun; w: 1 when shadows are on
    vec4 sun_color;           // rgb: colour * intensity; a: 1 when a sun exists
    vec4 cascade_splits;      // xyz: view depth where each cascade ends
    vec4 cascade_texels;      // xyz: world size of one shadow texel per cascade
    vec4 cascade_bias;        // xyz: depth bias per cascade
    vec4 sky_zenith;          // rgb (already scaled by the ambient intensity)
    vec4 sky_horizon;
    vec4 sky_ground;
    vec4 irradiance_sh[9];    // sky irradiance / pi as spherical harmonics (rgb)
    uvec4 counts;             // x: point lights in use
    PointLight points[16];
} frame;

// Sky colour toward `dir` (unit). Keep in sync with sky_radiance() in
// engine/render/src/light.cpp.
vec3 sky_gradient(vec3 dir) {
    if (dir.y >= 0.0) return mix(frame.sky_horizon.rgb, frame.sky_zenith.rgb, sqrt(dir.y));
    return mix(frame.sky_horizon.rgb, frame.sky_ground.rgb, smoothstep(0.0, 0.12, -dir.y));
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

layout(push_constant) uniform Push {
    mat4 model;
    vec4 base_color;      // rgb: linear base colour; a: selection highlight (0..1)
    vec4 emissive_rough;  // rgb: emitted colour; a: roughness
    uvec4 material;       // x: base colour texture, y: metallic-roughness texture,
                          // z: normal texture (0 = none), w: metallic (float bits)
                          // Shadow pass: x = cascade index.
} pc;
