#version 440

// Unit quad corner, per vertex.
layout(location = 0) in vec2 corner;
// Per tile instance.
layout(location = 1) in vec4 dst;       // canvas x, y, width, height
layout(location = 2) in vec4 uvRect;    // u0, v0, u1, v1 in the atlas page
layout(location = 3) in vec4 clampRect; // umin, vmin, umax, vmax (half-texel inset)

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vClamp;
layout(location = 2) out vec2 vCanvas;

layout(std140, binding = 0) uniform Frame {
    mat4 mvp;       // canvas pixels -> clip space
    vec4 canvas;    // width, height, unused, unused
    vec4 checker;   // cell size (framebuffer px), light, dark (linear), unused
} frame;

void main()
{
    vec2 p = dst.xy + corner * dst.zw;
    vCanvas = p;
    vUv = mix(uvRect.xy, uvRect.zw, corner);
    vClamp = clampRect;
    gl_Position = frame.mvp * vec4(p, 0.0, 1.0);
}
