#version 450

layout(location = 0) in vec2 v_uv;
layout(binding = 0) uniform sampler2D u_color;
layout(location = 0) out vec4 out_color;

float bayer4(vec2 p) {
    int x = int(mod(p.x, 4.0));
    int y = int(mod(p.y, 4.0));
    int m[16] = int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
    return float(m[y*4 + x]) / 16.0;
}

void main() {
    vec3 c = texture(u_color, v_uv).rgb;
    float d = bayer4(gl_FragCoord.xy) - 0.5;
    c += d * 0.02;
    c = floor(c * 16.0) / 16.0;
    out_color = vec4(c, 1.0);
}
