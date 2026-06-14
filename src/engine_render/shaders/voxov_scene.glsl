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

// ── Atmosphere parameters (binding 2, std140) ────────────────────────
// Rayleigh + Mie scattering for aerial perspective.
// CPU pre-computes sky color; fragment shader attenuates terrain.
layout(binding=2) uniform atm_params {
    vec4 planet_center_radius;      // xyz=planet center, w=radius
    vec4 atm_params_1;              // x=atm_h, y=H_R, z=H_M, w=g
    vec4 rayleigh_scatter_unused;   // xyz=β_R
    vec4 mie_scatter_pad;           // x=β_M
    vec4 sun_dir_intensity;         // xyz=sun_dir, w=intensity
};

in vec3 v_color;
in vec3 v_normal;
in vec3 v_world_pos;
out vec4 frag_color;

// ── Atmospheric transmittance ────────────────────────────────────────
// Ray march through atmosphere shell, exponential density falloff.
// Returns transmittance (1=clear, 0=fully attenuated).
vec3 atmosphere_transmittance(vec3 start, vec3 end) {
    vec3 dir = end - start;
    float dist = length(dir);
    if (dist < 0.001) return vec3(1.0);

    vec3 step_dir = dir / dist;
    float step_size = dist / 8.0;
    vec3 opt_depth = vec3(0.0);
    float opt_depth_mie = 0.0;

    float R = planet_center_radius.w;
    vec3 center = planet_center_radius.xyz;
    float Hr = atm_params_1.y;
    float Hm = atm_params_1.z;
    vec3 betaR = rayleigh_scatter_unused.xyz;
    float betaM = mie_scatter_pad.x;

    for (int i = 0; i < 8; i++) {
        float t = (float(i) + 0.5) * step_size;
        vec3 p = start + step_dir * t;
        float h = length(p - center) - R;
        if (h < 0.0) break;

        float dr = exp(-h / Hr) * step_size;
        float dm = exp(-h / Hm) * step_size;
        opt_depth += betaR * dr;
        opt_depth_mie += betaM * dm;
    }
    opt_depth_mie *= 1.1;
    return exp(-(opt_depth + vec3(opt_depth_mie)));
}

void main() {
    // Aerial perspective: attenuate terrain by atmosphere along view ray.
    vec3 atm_trans = atmosphere_transmittance(camera_pos, v_world_pos);

    float normal_len2 = dot(v_normal, v_normal);
    if (normal_len2 < 0.001) {
        frag_color = vec4(v_color * atm_trans, 1.0);
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

    frag_color = vec4(v_color * lit * atm_trans, 1.0);
}
@end

@program voxov_scene vs fs
