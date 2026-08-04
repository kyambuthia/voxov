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
in vec3 texcoord0;

out vec3 v_color;
out vec3 v_normal;
out vec3 v_world_pos;
out vec3 v_texcoord;

void main() {
    vec4 world_pos = model * vec4(position, 1.0);
    mat4 normal_model = model;
    normal_model[3] = vec4(0.0, 0.0, 0.0, 1.0);

    v_color = color0;
    v_normal = mat3(normal_model) * normal;
    v_world_pos = world_pos.xyz;
    v_texcoord = texcoord0;
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
    vec4 render_flags;
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

layout(binding=0) uniform texture2DArray voxel_texture;
layout(binding=0) uniform sampler voxel_sampler;

in vec3 v_color;
in vec3 v_normal;
in vec3 v_world_pos;
in vec3 v_texcoord;
out vec4 frag_color;

vec3 apply_view_treatment(vec3 color) {
    if (render_flags.x > 0.5) {
        const float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
        return mix(color, vec3(luma), 0.98);
    }
    return color;
}

// ── Atmospheric transmittance ────────────────────────────────────────
// Ray march through atmosphere shell, exponential density falloff.
// Returns transmittance (1=clear, 0=fully attenuated).
vec3 atmosphere_transmittance(vec3 start, vec3 end) {
    vec3 dir = end - start;
    float dist = length(dir);
    if (dist < 0.001) return vec3(1.0);

    float R = planet_center_radius.w;
    vec3 center = planet_center_radius.xyz;
    float Hr = atm_params_1.y;
    float Hm = atm_params_1.z;
    vec3 betaR = rayleigh_scatter_unused.xyz;
    float betaM = mie_scatter_pad.x;

    float start_h = max(length(start - center) - R, 0.0);
    float end_h = max(length(end - center) - R, 0.0);
    float density_r = 0.5 * (exp(-start_h / Hr) + exp(-end_h / Hr));
    float density_m = 0.5 * (exp(-start_h / Hm) + exp(-end_h / Hm));
    vec3 optical_depth = betaR * dist * density_r +
                         vec3(betaM * 1.1 * dist * density_m);
    return exp(-optical_depth);
}

void main() {
    // Aerial perspective: attenuate terrain by atmosphere along view ray.
    vec3 atm_trans = atmosphere_transmittance(camera_pos, v_world_pos);
    // Preserve nearby voxel readability at dense, low-altitude sight lines.
    atm_trans = max(atm_trans, vec3(0.35));

    float normal_len2 = dot(v_normal, v_normal);
    if (normal_len2 < 0.001) {
        frag_color = vec4(apply_view_treatment(v_color * atm_trans), 1.0);
        return;
    }

    vec3 base_color = v_color;
    if (v_texcoord.z >= 0.0) {
        vec2 tiled_uv = fract(v_texcoord.xy);
        vec3 texel;
        if (v_texcoord.z >= 2.5) {
            vec3 soil = texture(
                sampler2DArray(voxel_texture, voxel_sampler),
                vec3(tiled_uv, 1.0)).rgb;
            vec3 turf = texture(
                sampler2DArray(voxel_texture, voxel_sampler),
                vec3(tiled_uv, 0.0)).rgb;
            texel = mix(soil, turf, step(0.78, tiled_uv.y));
        } else {
            texel = texture(
                sampler2DArray(voxel_texture, voxel_sampler),
                vec3(tiled_uv, v_texcoord.z)).rgb;
        }
        base_color = pow(texel, vec3(0.78)) *
                     mix(vec3(1.0), v_color, 0.18);
    }

    vec3 n = normalize(v_normal);
    vec3 sun_dir = normalize(sun_dir_intensity.xyz);
    vec3 l = normalize(light_direction);
    vec3 v = normalize(camera_pos - v_world_pos);
    vec3 h = normalize(sun_dir + v);

    float ndl = clamp(max(dot(n, l), 0.0) * 0.35 +
                      max(dot(n, sun_dir), 0.0) * 0.90, 0.0, 1.25);
    float ndh = max(dot(n, h), 0.0);
    float spec_norm = (material_shininess + 8.0) * 0.0397887358;
    float spec_factor = spec_norm * pow(ndh, material_shininess) * ndl;

    vec3 ambient = light_ambient * material_ambient;
    vec3 diffuse = light_diffuse * material_diffuse * ndl;
    vec3 specular = light_specular * material_specular * spec_factor;
    float sun_boost = max(sun_dir_intensity.w / 20.0, 0.0);
    vec3 lit = (ambient + diffuse + specular) * sun_boost;

    vec3 terrain_lit = base_color * lit * atm_trans;
    float fog_amount = clamp(
        (1.0 - dot(atm_trans, vec3(0.333333))) * 0.55, 0.0, 0.72);
    vec3 aerial_color = vec3(0.42, 0.61, 0.88);
    frag_color = vec4(apply_view_treatment(
        mix(terrain_lit, aerial_color, fog_amount)), 1.0);
}
@end

@program voxov_scene vs fs
