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

    // Integrate only the segment inside the finite atmosphere. Endpoint
    // averaging fogged empty space on long planet-to-planet sight lines.
    vec3 direction = dir / dist;
    vec3 origin = start - center;
    float b = dot(origin, direction);
    float outer_radius = R + atm_params_1.x;
    float discriminant = b * b - dot(origin, origin) + outer_radius * outer_radius;
    if (discriminant <= 0.0) return vec3(1.0);
    float root = sqrt(discriminant);
    float entry = max(0.0, -b - root);
    float exit_atm = min(dist, -b + root);
    if (exit_atm <= entry) return vec3(1.0);
    float step_length = (exit_atm - entry) / 4.0;
    vec3 optical_depth = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float altitude = max(0.0, length(origin + direction *
            (entry + (float(i) + 0.5) * step_length)) - R);
        optical_depth += (betaR * exp(-altitude / max(Hr, 0.001)) +
            vec3(betaM * 1.1 * exp(-altitude / max(Hm, 0.001)))) * step_length;
    }
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
        // Layer 3 is the composite grass side: dirt with a turf lip.
        if (v_texcoord.z >= 2.5 && v_texcoord.z < 3.5) {
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

// A background triangle needs no vertex buffer and never writes terrain depth.
@vs sky_vs
layout(binding=0) uniform sky_vs_params {
    mat4 inverse_view_projection;
};
out vec3 sky_ray;
void main() {
    vec2 corner = vec2((gl_VertexIndex & 1) != 0 ? 3.0 : -1.0,
                       (gl_VertexIndex & 2) != 0 ? 3.0 : -1.0);
    // An interior clip depth avoids w=0 when a very distant far plane rounds
    // to infinity in the float projection (the sky-navigation view).
    vec4 ray = inverse_view_projection * vec4(corner, 0.0, 1.0);
    sky_ray = ray.xyz / ray.w;
    gl_Position = vec4(corner, 0.5, 1.0);
}
@end

@fs sky_fs
layout(binding=1) uniform sky_fs_params {
    vec4 sky_camera_radius; // xyz=planet-relative camera, w=radius
    vec4 sky_sun_height;    // xyz=sun direction, w=atmosphere height
    vec4 sky_style;         // x=grayscale, y=time
};
in vec3 sky_ray;
out vec4 frag_color;

float sky_hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec2 sky_sphere(vec3 origin, vec3 direction, float radius) {
    float b = dot(origin, direction);
    float d = b * b - dot(origin, origin) + radius * radius;
    if (d < 0.0) return vec2(-1.0);
    float root = sqrt(d);
    return vec2(-b - root, -b + root);
}

void main() {
    vec3 ray = normalize(sky_ray);
    // Angular cells are anchored in world space, so blocks and stars remain
    // still when the camera moves or rolls around a cube-sphere face.
    vec3 cell = floor(ray * 256.0 + 0.5);
    vec3 block_ray = normalize(cell);
    vec3 origin = sky_camera_radius.xyz;
    float radius = sky_camera_radius.w;
    float height = max(sky_sun_height.w, 0.001);
    vec3 sun = normalize(sky_sun_height.xyz);
    vec2 shell = sky_sphere(origin, block_ray, radius + height);
    vec2 ground = sky_sphere(origin, block_ray, radius);
    float entry = max(0.0, shell.x);
    float exit_atm = shell.y;
    if (ground.x > 0.0) exit_atm = min(exit_atm, ground.x);

    vec3 scatter = vec3(0.0);
    float optical_depth = 0.0;
    float step_length = max(0.0, exit_atm - entry) / 8.0;
    float mu = dot(block_ray, sun);
    for (int i = 0; i < 8; ++i) {
        vec3 point = origin + block_ray *
            (entry + (float(i) + 0.5) * step_length);
        float altitude = max(0.0, length(point) - radius);
        float density = exp(-altitude / (height * 0.25));
        float depth = density * step_length / height;
        optical_depth += depth;
        vec2 shadow = sky_sphere(point, sun, radius);
        float lit = shadow.x > 0.001 ? 0.0 : 1.0;
        float phase = 0.75 * (1.0 + mu * mu);
        vec3 rayleigh = vec3(0.28, 0.65, 1.65) * phase;
        vec3 haze = vec3(0.85, 0.68, 0.48) * pow(max(mu, 0.0), 16.0);
        scatter += (rayleigh + haze) * lit * depth * exp(-optical_depth);
    }

    vec3 color = vec3(0.002, 0.003, 0.009);
    float star = step(0.9985, sky_hash(cell));
    color += vec3(0.7, 0.8, 1.0) * star;
    // The sun is a square in its tangent plane, matching the voxel sky.
    vec3 sun_right = normalize(cross(sun,
        abs(sun.y) < 0.95 ? vec3(0, 1, 0) : vec3(0, 0, 1)));
    vec3 sun_up = cross(sun_right, sun);
    float sun_square = step(max(abs(dot(ray, sun_right)),
                               abs(dot(ray, sun_up))), 0.014) * step(0.0, mu);
    color += vec3(10.0, 7.0, 3.5) * sun_square;
    color = color * exp(-optical_depth * 5.0) + scatter * 4.0;

    // One world-anchored cloud shell, broken into cubical clusters. Sample
    // the first forward intersection so clouds also read from orbit.
    vec2 clouds = sky_sphere(origin, block_ray, radius + height * 0.55);
    float cloud_t = clouds.x > 0.0 ? clouds.x : clouds.y;
    if (cloud_t > 0.0 && (ground.x < 0.0 || cloud_t < ground.x)) {
        vec3 cloud_point = origin + block_ray * cloud_t;
        vec3 cloud_cell = floor((cloud_point +
            vec3(sky_style.y * 0.12, 0.0, sky_style.y * 0.06)) /
            max(height * 0.12, 0.5));
        float cluster = sky_hash(floor(cloud_cell / 3.0));
        float cloud = step(0.60, cluster) * step(0.22, sky_hash(cloud_cell));
        float day = smoothstep(-0.15, 0.2, dot(normalize(cloud_point), sun));
        vec3 cloud_color = mix(vec3(0.018, 0.025, 0.045),
                              vec3(1.8, 1.7, 1.5), day);
        color = mix(color, cloud_color, cloud * exp(-optical_depth * 0.4) * 0.85);
    }
    color = color / (vec3(1.0) + color);
    if (sky_style.x > 0.5) color = vec3(dot(color, vec3(0.2126, 0.7152, 0.0722)));
    frag_color = vec4(color, 1.0);
}
@end
@program blocky_sky sky_vs sky_fs
