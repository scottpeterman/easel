#version 440

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vClamp;
layout(location = 2) in vec2 vCanvas;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Frame {
    mat4 mvp;
    vec4 canvas;
    vec4 checker;
} frame;

layout(binding = 1) uniform sampler2D atlas;

float encodeSrgb(float c)
{
    c = clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

void main()
{
    if (vCanvas.x < 0.0 || vCanvas.y < 0.0 || vCanvas.x >= frame.canvas.x || vCanvas.y >= frame.canvas.y)
        discard;

    // Tiles are linear-light, premultiplied. Clamping keeps bilinear
    // filtering from reading the neighbouring atlas slot.
    vec4 c = texture(atlas, clamp(vUv, vClamp.xy, vClamp.zw));

    // Transparency shows a screen-space checkerboard, composited in linear light.
    vec2 cell = floor(gl_FragCoord.xy / frame.checker.x);
    float bg = mod(cell.x + cell.y, 2.0) < 0.5 ? frame.checker.y : frame.checker.z;
    vec3 lin = c.rgb + vec3(bg) * (1.0 - c.a);

    fragColor = vec4(encodeSrgb(lin.r), encodeSrgb(lin.g), encodeSrgb(lin.b), 1.0);
}
