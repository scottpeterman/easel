#version 440

layout(location = 0) in vec2 position; // canvas pixels

layout(std140, binding = 0) uniform Frame {
    mat4 mvp;
    vec4 canvas;
    vec4 checker;
} frame;

void main()
{
    gl_Position = frame.mvp * vec4(position, 0.0, 1.0);
}
