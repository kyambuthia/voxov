@module voxov_scene
@ctype mat4 glm::mat4
@ctype vec3 glm::vec3

@vs vs
layout(binding=0) uniform vs_params {
    mat4 mvp;
    mat4 model;
    vec4 vegetation_params; // x=time, y=vegetation enable
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
    vec3 animated_position = position;
    if (vegetation_params.y > 0.5) {
        vec3 up = normalize(normal);
        vec3 wind_reference = normalize(vec3(0.73, 0.19, 0.61));
        vec3 wind_direction = wind_reference - up * dot(wind_reference, up);
        if (dot(wind_direction, wind_direction) < 0.001) {
            wind_direction = normalize(cross(up, vec3(1.0, 0.0, 0.0)));
        } else {
            wind_direction = normalize(wind_direction);
        }
        // UV.y runs from the planted base to the blade tip. Weighting wind by
        // the horizontal coordinate shears one side of the billboard and
        // makes the plant look disconnected from its voxel block.
        float weight = clamp(1.0 - texcoord0.y, 0.0, 1.0);
        float phase = dot(position, vec3(0.17, 0.11, 0.13)) +
                      texcoord0.y * 6.2831853;
        float gust = sin(vegetation_params.x * 2.2 + phase) * 0.5 +
                     sin(vegetation_params.x * 0.83 + phase * 0.47) * 0.25;
            animated_position += wind_direction * gust * 0.06 * weight * weight;
    }

    vec4 world_pos = model * vec4(animated_position, 1.0);
    mat4 normal_model = model;
    normal_model[3] = vec4(0.0, 0.0, 0.0, 1.0);

    v_color = color0;
    v_normal = mat3(normal_model) * normal;
    v_world_pos = world_pos.xyz;
    v_texcoord = texcoord0;
    gl_Position = mvp * vec4(animated_position, 1.0);
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
layout(binding=1) uniform texture2DArray vegetation_texture;
layout(binding=1) uniform sampler vegetation_sampler;

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

float impostor_frame_layer(float base_layer) {
    // The plant normal is a billboard-card normal. Together with the radial
    // surface up-vector it forms a stable local frame, so the selected yaw
    // does not change when the planet is viewed from a different cube face.
    vec3 plant_up = normalize(v_world_pos - planet_center_radius.xyz);
    vec3 card_normal = v_normal - plant_up * dot(v_normal, plant_up);
    if (dot(card_normal, card_normal) < 0.001) return base_layer;
    card_normal = normalize(card_normal);
    vec3 card_right = normalize(cross(plant_up, card_normal));
    vec3 view_direction = normalize(camera_pos - v_world_pos);
    vec3 horizontal_view = view_direction -
                           plant_up * dot(view_direction, plant_up);
    if (dot(horizontal_view, horizontal_view) < 0.001) {
        horizontal_view = card_normal;
    } else {
        horizontal_view = normalize(horizontal_view);
    }
    const float kTau = 6.2831853;
    float yaw = atan(dot(horizontal_view, card_right),
                     dot(horizontal_view, card_normal));
    float frame = mod(floor((yaw + 3.14159265) * (8.0 / kTau)), 8.0);
    return base_layer + frame;
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
    // Debug text carries coverage in texcoord.x and uses a negative texture
    // layer sentinel. Render it as alpha-blended coverage so TTF antialiasing
    // survives rasterization instead of becoming opaque block pixels.
    if (v_texcoord.z < -1.5) {
        frag_color = vec4(v_color, clamp(v_texcoord.x, 0.0, 1.0));
        return;
    }

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
    float output_alpha = 1.0;
    if (render_flags.y > 0.5) {
        float vegetation_layer = v_texcoord.z;
        if (vegetation_layer >= 6.0) {
            vegetation_layer = impostor_frame_layer(vegetation_layer);
        }
        vec4 plant_tex = texture(
            sampler2DArray(vegetation_texture, vegetation_sampler),
            vec3(fract(v_texcoord.xy), vegetation_layer));
        if (plant_tex.a < 0.35) discard;
        base_color = pow(plant_tex.rgb, vec3(0.78)) * v_color;
        output_alpha = plant_tex.a;
        if (v_texcoord.z < 2.0 ||
            (v_texcoord.z >= 6.0 && v_texcoord.z < 14.0)) {
            // Keep generated foliage in the same muted olive family as the
            // voxel grass block instead of letting bright tips dominate it.
            base_color = mix(base_color, vec3(0.34, 0.46, 0.12), 0.22);
        }
    } else if (v_texcoord.z >= 0.0) {
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

    float ndl = render_flags.y > 0.5
        ? clamp(abs(dot(n, l)) * 0.35 + abs(dot(n, sun_dir)) * 0.90,
                0.0, 1.25)
        : clamp(max(dot(n, l), 0.0) * 0.35 +
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
        mix(terrain_lit, aerial_color, fog_amount)), output_alpha);
}
@end

@program voxov_scene vs fs
