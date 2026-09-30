#version 440

layout(location = 0) in vec2 position; // canvas pixels
layout(location = 1) in vec4 color;    // sRGB-encoded, straight alpha

layout(location = 0) out vec4 vColor;

layout(std140, binding = 0) uniform Frame {
    mat4 mvp;
    vec4 canvas;
    vec4 checker;
} frame;

void main()
{
    vColor = color;
    gl_Position = frame.mvp * vec4(position, 0.0, 1.0);
}
