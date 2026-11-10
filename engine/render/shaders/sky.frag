#version 450
#extension GL_GOOGLE_include_directive : require
// Sky background: the Environment's gradient plus the sun's disc and glow.

#include "common.glsl"

layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 out_color;

void main() {
    const vec4 far = frame.inv_view_proj * vec4(v_ndc, 1.0, 1.0);
    const vec3 dir = normalize(far.xyz / far.w - frame.camera_position.xyz);
    vec3 color = sky_gradient(dir);
    if (frame.sun_color.a > 0.5) {
        const float c = dot(dir, frame.sun_direction.xyz);
        const float disc = smoothstep(0.99975, 0.99990, c);       // about 1 degree across
        const float glow = pow(max(c, 0.0), 350.0) * 0.6 + pow(max(c, 0.0), 12.0) * 0.06;
        color += frame.sun_color.rgb * (disc * 8.0 + glow);
    }
    out_color = vec4(aces(color * frame.camera_position.w), 1.0);
}
