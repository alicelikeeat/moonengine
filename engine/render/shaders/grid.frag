#version 450
// Anti-aliased analytic grid: 1 m minor lines, 10 m major lines, red X axis
// and blue Z axis, fading out with distance from the camera.

layout(location = 0) in vec3 v_world;

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera_position;
    vec4 unused1;
    vec4 unused2;
    vec4 unused3;
} pc;

layout(location = 0) out vec4 out_color;

// Coverage (0..1) of grid lines of the given cell size at this fragment.
float grid_lines(vec2 coord, float cell) {
    vec2 c = coord / cell;
    vec2 width = fwidth(c);
    vec2 g = abs(fract(c - 0.5) - 0.5) / max(width, vec2(1e-6));
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
    vec2 coord = v_world.xz;
    float minor = grid_lines(coord, 1.0);
    float major = grid_lines(coord, 10.0);
    vec2 d = fwidth(coord);
    float x_axis = 1.0 - min(abs(coord.y) / max(d.y, 1e-6), 1.0);  // line z = 0
    float z_axis = 1.0 - min(abs(coord.x) / max(d.x, 1e-6), 1.0);  // line x = 0

    vec3 color = vec3(0.26);
    float alpha = minor * 0.55;
    if (major > 0.0) { color = mix(color, vec3(0.42), major); alpha = max(alpha, major * 0.8); }
    if (x_axis > 0.0) { color = mix(color, vec3(0.80, 0.18, 0.18), x_axis); alpha = max(alpha, x_axis); }
    if (z_axis > 0.0) { color = mix(color, vec3(0.20, 0.38, 0.90), z_axis); alpha = max(alpha, z_axis); }

    // Fade with distance; farther when the camera is higher above the ground.
    float dist = length(v_world - pc.camera_position.xyz);
    float reach = max(40.0, abs(pc.camera_position.y) * 12.0);
    alpha *= 1.0 - smoothstep(reach * 0.35, reach, dist);
    if (alpha <= 0.002) discard;
    out_color = vec4(color, alpha);
}
