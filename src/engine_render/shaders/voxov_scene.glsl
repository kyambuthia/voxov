@module voxov_scene
@ctype mat4 glm::mat4
@ctype vec3 glm::vec3

@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    mat4 model;
};

in vec3 position;
in vec3 color0;
in vec3 normal;

out vec3 v_color;
out vec3 v_normal;
out vec3 v_world_pos;

void main() {
    vec4 world_pos = model * vec4(position, 1.0);
    mat4 normal_model = model;
    normal_model[3] = vec4(0.0, 0.0, 0.0, 1.0);

    v_color = color0;
    v_normal = mat3(normal_model) * normal;
    v_world_pos = world_pos.xyz;
    gl_Position = mvp * vec4(position, 1.0);
}
@end

@fs fs
layout(binding=1) uniform fs_params {
    vec3 light_direction;
    vec3 light_ambient;
    vec3 light_diffuse;
    vec3 light_specular;
    vec3 material_ambient;
    vec3 material_diffuse;
    vec3 material_specular;
    float material_shininess;
    vec3 camera_pos;
};

in vec3 v_color;
in vec3 v_normal;
in vec3 v_world_pos;
out vec4 frag_color;

void main() {
    float normal_len2 = dot(v_normal, v_normal);
    if (normal_len2 < 0.001) {
        frag_color = vec4(v_color, 1.0);
        return;
    }

    vec3 n = normalize(v_normal);
    vec3 l = normalize(light_direction);
    vec3 v = normalize(camera_pos - v_world_pos);
    vec3 h = normalize(l + v);

    float ndl = max(dot(n, l), 0.0);
    float ndh = max(dot(n, h), 0.0);
    float spec_norm = (material_shininess + 8.0) * 0.0397887358;
    float spec_factor = spec_norm * pow(ndh, material_shininess) * ndl;

    vec3 ambient = light_ambient * material_ambient;
    vec3 diffuse = light_diffuse * material_diffuse * ndl;
    vec3 specular = light_specular * material_specular * spec_factor;
    vec3 lit = ambient + diffuse + specular;

    frag_color = vec4(v_color * lit, 1.0);
}
@end

@program voxov_scene vs fs
