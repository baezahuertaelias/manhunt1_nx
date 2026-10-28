#version 460
layout(location=1) in vec2 uv;
layout(binding=0) uniform sampler2D image;
layout(std140, binding=0) uniform Settings { vec4 dimensions; vec4 textureState; };
layout(location=0) out vec4 outColour;
void main()
{
    vec4 c = texture(image, uv / textureState.xy);
    // Match the guest alpha comparator's eight-bit quantization, not float > ref.
    if (textureState.z != 0.0 && int(c.a * 255.0 + 0.5) <= int(textureState.w))
        discard;
    outColour = c;
}
