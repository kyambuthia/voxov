#version 450

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    vec3 n = normalize(v_normal);
    float ndl = clamp(dot(n, normalize(vec3(0.3, 0.8, 0.4))), 0.0, 1.0);
    float stepped = floor(ndl * 4.0) / 4.0;
    float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 2.0);
    vec3 base = v_color * (0.5 + 0.5 * stepped);
    vec3 lit = base + rim * 0.15;
    out_color = vec4(lit, 1.0);
}
