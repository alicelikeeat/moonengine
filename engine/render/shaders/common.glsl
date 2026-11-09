// Shared declarations for the scene shaders. Must match FrameUniforms and
// PushConstants in engine/render/src/vulkan_renderer.cpp.

struct PointLight {
    vec4 position_range;  // xyz: world position, w: range
    vec4 color;           // rgb: colour * intensity
};

layout(set = 0, binding = 1, std140) uniform Frame {
    mat4 view_proj;
    mat4 light_view_proj;  // world -> sun shadow map clip space
    vec4 camera_position;  // xyz; w: exposure
    vec4 sun_direction;    // xyz: unit vector toward the sun; w: 1 when shadows are on
    vec4 sun_color;        // rgb: colour * intensity
    vec4 sky_color;        // rgb: ambient from above (already scaled)
    vec4 ground_color;     // rgb: ambient from below
    vec4 shadow_params;    // x: texel size (uv), y: texel size (world units), z: depth bias
    uvec4 counts;          // x: point lights in use
    PointLight points[16];
} frame;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 base_color;      // rgb: linear base colour; a: selection highlight (0..1)
    vec4 emissive_rough;  // rgb: emitted colour; a: roughness
    uvec4 material;       // x: base colour texture, y: metallic-roughness texture,
                          // z: normal texture (0 = none), w: metallic (float bits)
} pc;
