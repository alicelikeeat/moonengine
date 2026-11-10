#version 450
#extension GL_GOOGLE_include_directive : require
// Scene fragment shader: metallic-roughness PBR (GGX specular, Lambert
// diffuse), one directional light with cascaded shadow maps, up to 16 point
// lights, image-based lighting from the sky (spherical-harmonic irradiance
// and a prefiltered cube map), ACES tone mapping.

#include "common.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_tangent;

layout(set = 0, binding = 0) uniform sampler2D textures[1024];
layout(set = 0, binding = 2) uniform sampler2DArrayShadow shadow_map;
layout(set = 0, binding = 3) uniform samplerCube sky_specular;  // mip = roughness * 5

layout(location = 0) out vec4 out_color;

const float PI = 3.14159265;

float d_ggx(float n_dot_h, float a) {
    const float a2 = a * a;
    const float d = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Height-correlated Smith visibility (includes the 1 / (4 NoL NoV) term).
float v_smith(float n_dot_v, float n_dot_l, float a) {
    const float a2 = a * a;
    const float gl = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - a2) + a2);
    const float gv = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - a2) + a2);
    return 0.5 / max(gl + gv, 1e-5);
}

vec3 f_schlick(vec3 f0, float v_dot_h) {
    return f0 + (1.0 - f0) * pow(1.0 - v_dot_h, 5.0);
}

// Radiance reflected toward V from light arriving along L with irradiance 1.
vec3 shade(vec3 n, vec3 v, vec3 l, vec3 base, float metallic, float roughness) {
    const float n_dot_l = clamp(dot(n, l), 0.0, 1.0);
    if (n_dot_l <= 0.0) return vec3(0.0);
    const vec3 h = normalize(v + l);
    const float n_dot_v = max(dot(n, v), 1e-4);
    const float a = roughness * roughness;
    const vec3 f0 = mix(vec3(0.04), base, metallic);
    const vec3 f = f_schlick(f0, clamp(dot(v, h), 0.0, 1.0));
    const vec3 specular = d_ggx(clamp(dot(n, h), 0.0, 1.0), a) * v_smith(n_dot_v, n_dot_l, a) * f;
    const vec3 diffuse = (1.0 - f) * (1.0 - metallic) * base / PI;
    return (diffuse + specular) * n_dot_l;
}

// Split-sum environment BRDF, analytic fit (Karis, "Physically Based
// Shading on Mobile").
vec2 env_brdf(float roughness, float n_dot_v) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    const vec4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// Sky irradiance / pi around n (Ramamoorthi & Hanrahan; the cosine
// convolution is folded into the coefficients).
vec3 sky_diffuse(vec3 n) {
    vec3 e = frame.irradiance_sh[0].rgb;
    e += frame.irradiance_sh[1].rgb * n.y + frame.irradiance_sh[2].rgb * n.z + frame.irradiance_sh[3].rgb * n.x;
    e += frame.irradiance_sh[4].rgb * (n.x * n.y) + frame.irradiance_sh[5].rgb * (n.y * n.z);
    e += frame.irradiance_sh[6].rgb * (3.0 * n.z * n.z - 1.0);
    e += frame.irradiance_sh[7].rgb * (n.x * n.z) + frame.irradiance_sh[8].rgb * (n.x * n.x - n.y * n.y);
    return max(e, vec3(0.0));
}

float sun_visibility(vec3 n, vec3 l) {
    if (frame.sun_direction.w < 0.5) return 1.0;
    const float view_depth = dot(v_world - frame.camera_position.xyz, frame.camera_forward.xyz);
    const int cascade = view_depth < frame.cascade_splits.x ? 0 : (view_depth < frame.cascade_splits.y ? 1 : 2);
    if (view_depth >= frame.cascade_splits.z) return 1.0;
    // Normal offset (more at grazing angles) plus a constant depth bias.
    const float slope = 1.0 - clamp(dot(n, l), 0.0, 1.0);
    const vec3 p = v_world + n * frame.cascade_texels[cascade] * (1.0 + 2.0 * slope);
    const vec4 clip = frame.light_view_proj[cascade] * vec4(p, 1.0);
    const vec2 uv = clip.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || clip.z > 1.0) return 1.0;
    const float depth = clip.z - frame.cascade_bias[cascade];
    const float texel = 1.0 / float(textureSize(shadow_map, 0).x);
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(shadow_map, vec4(uv + vec2(x, y) * texel, float(cascade), depth));
    lit /= 9.0;
    // Fade out at the end of the last cascade.
    return mix(lit, 1.0, smoothstep(frame.cascade_splits.z * 0.9, frame.cascade_splits.z, view_depth));
}

void main() {
    const vec3 base = pc.base_color.rgb * texture(textures[pc.material.x], v_uv).rgb;
    const vec4 mr = texture(textures[pc.material.y], v_uv);
    const float metallic = clamp(uintBitsToFloat(pc.material.w) * mr.b, 0.0, 1.0);
    const float roughness = clamp(pc.emissive_rough.a * mr.g, 0.045, 1.0);

    vec3 n = normalize(v_normal);
    if (pc.material.z != 0u) {
        vec3 t = v_tangent.xyz - n * dot(n, v_tangent.xyz);
        if (dot(t, t) > 1e-12) {
            t = normalize(t);
            const vec3 b = cross(n, t) * v_tangent.w;
            const vec3 tn = texture(textures[pc.material.z], v_uv).xyz * 2.0 - 1.0;
            n = normalize(mat3(t, b, n) * tn);
        }
    }
    const vec3 v = normalize(frame.camera_position.xyz - v_world);

    vec3 color = vec3(0.0);
    const vec3 l_sun = frame.sun_direction.xyz;
    if (frame.sun_color.a > 0.5)
        color += shade(n, v, l_sun, base, metallic, roughness) * frame.sun_color.rgb * sun_visibility(n, l_sun);

    for (uint i = 0u; i < frame.counts.x; ++i) {
        const vec3 to_light = frame.points[i].position_range.xyz - v_world;
        const float d2 = dot(to_light, to_light);
        const float range = frame.points[i].position_range.w;
        const float ratio = d2 / (range * range);
        const float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
        const float attenuation = window * window / max(d2, 1e-4);
        if (attenuation <= 0.0) continue;
        color += shade(n, v, to_light * inversesqrt(d2), base, metallic, roughness) * frame.points[i].color.rgb * attenuation;
    }

    // Light from the sky: diffuse irradiance around n, and the prefiltered
    // sky along the reflection for specular.
    const float n_dot_v = max(dot(n, v), 1e-4);
    const vec3 f0 = mix(vec3(0.04), base, metallic);
    const vec2 ab = env_brdf(roughness, n_dot_v);
    const vec3 r = normalize(mix(reflect(-v, n), n, roughness * roughness));
    const vec3 reflected = textureLod(sky_specular, r, roughness * 5.0).rgb;
    color += base * (1.0 - metallic) * sky_diffuse(n) + reflected * (f0 * ab.x + ab.y);

    color += pc.emissive_rough.rgb;
    vec3 mapped = aces(color * frame.camera_position.w);
    // Selected objects are tinted toward the editor's orange selection colour.
    mapped = mix(mapped, vec3(1.0, 0.55, 0.12), 0.45 * pc.base_color.a);
    out_color = vec4(mapped, 1.0);
}
