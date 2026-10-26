#version 460

// Solid colour so the readback check can compare exact RGBA8 values.
layout (location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
