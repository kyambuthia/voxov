@module voxov_scene
@ctype mat4 glm::mat4

@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
};

in vec3 position;
in vec3 color0;
in vec3 normal;

out vec3 v_color;
out vec3 v_normal;

void main() {
    v_color = color0;
    v_normal = normal;
    gl_Position = mvp * vec4(position, 1.0);
}
@end

@fs fs
in vec3 v_color;
in vec3 v_normal;
out vec4 frag_color;

void main() {
    float normal_len2 = dot(v_normal, v_normal);
    if (normal_len2 < 0.001) {
        frag_color = vec4(v_color, 1.0);
        return;
    }
    vec3 n = normalize(v_normal);
    float ndl = clamp(dot(n, normalize(vec3(0.3, 0.8, 0.4))), 0.0, 1.0);
    float stepped = floor(ndl * 4.0) / 4.0;
    float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 2.0);
    vec3 base = v_color * (0.5 + 0.5 * stepped);
    frag_color = vec4(base + rim * 0.15, 1.0);
}
@end

@program voxov_scene vs fs
