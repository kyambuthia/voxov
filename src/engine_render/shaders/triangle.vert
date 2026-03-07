#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 v_color;
layout(location = 1) out vec3 v_normal;

layout(push_constant) uniform Push {
    mat4 view_proj;
} push_data;

void main() {
    gl_Position = push_data.view_proj * vec4(in_position, 1.0);
    v_color = in_color;
    v_normal = in_normal;
}
