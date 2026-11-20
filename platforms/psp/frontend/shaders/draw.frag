#version 450

layout(set = 0, binding = 0) uniform sampler2D u_texture;
layout(constant_id = 0) const bool TEXTURE_VARIANT = true;

layout(push_constant) uniform DrawPush {
    vec4 target_uv;
    vec4 uv_env;
    vec4 env_fog;
    vec4 fog_zrange;
    uvec4 flags;
    uvec4 alpha;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_specular;
layout(location = 3) in float v_fog;
layout(location = 0) out vec4 o_color;

bool compare_alpha(uint function, uint a, uint b) {
    if (function == 0u) return false;
    if (function == 1u) return true;
    if (function == 2u) return a == b;
    if (function == 3u) return a != b;
    if (function == 4u) return a < b;
    if (function == 5u) return a <= b;
    if (function == 6u) return a > b;
    return a >= b;
}

void main() {
    vec4 color = v_color;
    if (pc.flags.y == 0u) {
        if (TEXTURE_VARIANT && pc.flags.x != 0u) {
            vec4 texel = texture(u_texture, v_uv);
            uint function = pc.flags.z;
            bool rgba = (pc.flags.w & 1u) != 0u;
            if (function == 0u) {
                color.rgb *= texel.rgb;
                if (rgba) color.a *= texel.a;
            } else if (function == 1u) {
                color.rgb = rgba ? mix(color.rgb, texel.rgb, texel.a) : texel.rgb;
            } else if (function == 2u) {
                color.rgb = mix(color.rgb, vec3(pc.uv_env.z, pc.uv_env.w, pc.env_fog.x), texel.rgb);
                if (rgba) color.a *= texel.a;
            } else if (function == 3u) {
                color.rgb = texel.rgb;
                if (rgba) color.a = texel.a;
            } else {
                color.rgb += texel.rgb;
                if (rgba) color.a *= texel.a;
            }
            if ((pc.flags.w & 2u) != 0u) color.rgb *= 2.0;
        }
        color.rgb = min(color.rgb + v_specular, vec3(1.0));
        if (pc.alpha.w != 0u) color.rgb = mix(pc.env_fog.yzw, color.rgb, clamp(v_fog, 0.0, 1.0));
        if (pc.alpha.x != 1u) {
            uint alpha = uint(clamp(color.a, 0.0, 1.0) * 255.0 + 0.5) & pc.alpha.z;
            if (!compare_alpha(pc.alpha.x, alpha, pc.alpha.y & pc.alpha.z)) discard;
        }
        if ((pc.flags.w & 4u) != 0u) {
            float depth = gl_FragCoord.z * 65535.0;
            if (depth < pc.fog_zrange.y || depth > pc.fog_zrange.z + 0.5) discard;
        }
    }
    o_color = clamp(color, 0.0, 1.0);
}
