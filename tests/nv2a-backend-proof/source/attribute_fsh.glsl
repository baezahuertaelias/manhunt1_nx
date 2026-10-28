#version 460
layout(location=0) in vec4 colour;
layout(location=1) in vec2 uv;
layout(location=0) out vec4 outColour;
void main()
{
    // Unit-sized synthetic UVs exercise the attribute; no texture is sampled.
    outColour = vec4(0.75 * colour.rg + 0.25 * uv, colour.ba);
}
