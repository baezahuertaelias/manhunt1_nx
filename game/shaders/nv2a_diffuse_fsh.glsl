#version 460
layout(location=0) in vec4 colour;
layout(std140, binding=0) uniform Settings { vec4 dimensions; vec4 textureState; vec4 programState; };
layout(location=0) out vec4 outColour;
void main()
{
    vec4 c = clamp(colour, 0.0, 1.0);
    if (textureState.z != 0.0 && int(c.a * 255.0 + 0.5) <= int(textureState.w)) discard;
    // CPU packs the source before applying blend factors.
    outColour = floor(c * 255.0 + 0.5) / 255.0;
}
