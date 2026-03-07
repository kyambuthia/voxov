#version 450

layout(location = 0) in vec3 v_color;
layout(location = 1) in vec3 v_normal;
layout(location = 0) out vec4 out_color;

void main() {
    float lit = 1.0;
    if (dot(v_normal, v_normal) > 0.001) {
        vec3 n = normalize(v_normal);
        vec3 light_dir = normalize(vec3(0.35, 0.82, 0.24));
        lit = 0.35 + max(dot(n, light_dir), 0.0) * 0.65;
    }
    out_color = vec4(v_color * lit, 1.0);
}
