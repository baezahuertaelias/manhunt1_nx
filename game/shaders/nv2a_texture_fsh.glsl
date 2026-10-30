#version 460
layout(location=0) in vec4 colour;
layout(location=1) in vec2 uv;
layout(binding=0) uniform sampler2D image;
layout(std140, binding=0) uniform Settings { vec4 dimensions; vec4 textureState; vec4 programState; };
layout(location=0) out vec4 outColour;
void main()
{
    vec4 c;
    if (programState.x == 2.0) c = colour;
    else {
        if (programState.y != 0.0) {
            // Match CPU RC float interpolation before modulation and alpha test.
            vec2 p = clamp(uv, vec2(0.0), textureState.xy) - vec2(0.5);
            ivec2 i = ivec2(floor(p)), hi = ivec2(textureState.xy) - ivec2(1);
            vec2 w = fract(p);
            vec4 a = texelFetch(image, clamp(i, ivec2(0), hi), 0);
            vec4 b = texelFetch(image, clamp(i + ivec2(1, 0), ivec2(0), hi), 0);
            vec4 d = texelFetch(image, clamp(i + ivec2(0, 1), ivec2(0), hi), 0);
            vec4 e = texelFetch(image, clamp(i + ivec2(1, 1), ivec2(0), hi), 0);
            c = (a * (1.0 - w.x) + b * w.x) * (1.0 - w.y)
              + (d * (1.0 - w.x) + e * w.x) * w.y;
        } else c = texture(image, uv / textureState.xy);
        if (programState.x == 1.0) c *= max(colour, vec4(0.0));
    }
    c = clamp(c, 0.0, 1.0);
    // Match the guest alpha comparator's eight-bit quantization, not float > ref.
    if (textureState.z != 0.0 && int(c.a * 255.0 + 0.5) <= int(textureState.w))
        discard;
    // CPU packs the source before applying blend factors.
    outColour = floor(c * 255.0 + 0.5) / 255.0;
}
