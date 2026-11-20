#version 450

layout(location = 0) out vec2 v_uv;

void main() {
    vec2 position = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
    v_uv = vec2(position.x, 1.0 - position.y);
}
