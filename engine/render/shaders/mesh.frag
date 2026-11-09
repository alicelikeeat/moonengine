#version 450
// Mesh fragment shader: one directional light plus ambient.

layout(location = 0) in vec3 v_normal;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 normal0;
    vec4 normal1;
    vec4 normal2;
    vec4 color;
} pc;

layout(location = 0) out vec4 out_color;

void main() {
    const vec3 light_dir = normalize(vec3(0.4, 0.8, 0.6));
    float diffuse = max(dot(normalize(v_normal), light_dir), 0.0);
    out_color = vec4(pc.color.rgb * (0.25 + 0.75 * diffuse), 1.0);
}
