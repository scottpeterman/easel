#version 440

layout(location = 0) out vec4 fragColor;

void main()
{
    // Already sRGB-encoded: the render target stores display values.
    fragColor = vec4(30.0 / 255.0, 30.0 / 255.0, 30.0 / 255.0, 1.0);
}
