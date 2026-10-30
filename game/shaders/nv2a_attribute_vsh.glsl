#version 460
layout(location=0) in vec4 position; // surface x/y, unmapped guest z, rhw
layout(location=1) in vec4 diffuse;
layout(location=2) in vec2 texels;
layout(std140, binding=0) uniform Surface { vec4 dimensions; };
layout(location=0) out vec4 colour;
layout(location=1) out vec2 uv;
void main()
{
    float w = 1.0 / position.w;
    vec2 ndc = vec2(2.0 * position.x / dimensions.x - 1.0,
                    1.0 - 2.0 * position.y / dimensions.y);
    // Depth mapping is outside this proof; depth/stencil testing is disabled.
    gl_Position = vec4(ndc * w, 0.0, w);
    colour = diffuse;
    uv = texels;
}
