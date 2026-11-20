#version 450

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_specular;
layout(location = 4) in float a_fog;

layout(push_constant) uniform DrawPush {
    vec4 target_uv;
    vec4 uv_env;
    vec4 env_fog;
    vec4 fog_zrange;
    uvec4 flags;
    uvec4 alpha;
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec3 v_specular;
layout(location = 3) out float v_fog;

void main() {
    float perspective = a_position.w > 0.0 ? 1.0 / a_position.w : 1.0;
    vec2 ndc = a_position.xy / pc.target_uv.xy * 2.0 - 1.0;
    float depth = clamp(a_position.z / 65535.0, 0.0, 1.0);
    gl_Position = vec4(ndc * perspective, depth * perspective, perspective);
    v_uv = a_uv * pc.target_uv.zw + pc.uv_env.xy;
    v_color = a_color;
    v_specular = a_specular.rgb;
    v_fog = a_fog;
}
