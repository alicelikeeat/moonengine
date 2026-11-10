#version 450
// Full-screen triangle at the far plane for the sky background.

layout(location = 0) out vec2 v_ndc;

void main() {
    const vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    v_ndc = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
