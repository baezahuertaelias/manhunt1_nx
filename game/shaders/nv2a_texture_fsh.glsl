#version 460
layout(location=0) in vec4 colour;
layout(location=1) in vec2 uv;
layout(binding=0) uniform sampler2D image;
layout(std140, binding=0) uniform Settings { vec4 dimensions; vec4 textureState; vec4 programState; };
layout(location=0) out vec4 outColour;
// The CPU's wrap_coord (nv2a_pb_exec.c); programState.zw hold the modes. Plain integers, and division only on
// non-negative values: tests/deko-adapter/run.sh compiles this function as C against the runtime's.
int wrapFloorMod(int c, int n)
{
    int k = c >= 0 ? c / n : (-c + n - 1) / n;
    return c >= 0 ? c - k * n : c + k * n;
}
int wrapCoord(int c, int n, int mode)
{
    if (mode == 1) return wrapFloorMod(c, n);
    if (mode == 2) {
        int m = wrapFloorMod(c, 2 * n);
        return m < n ? m : 2 * n - 1 - m;
    }
    return c < 0 ? 0 : (c >= n ? n - 1 : c);
}
vec4 fetchWrapped(int x, int y)
{
    ivec2 size = ivec2(textureState.xy);
    return texelFetch(image, ivec2(wrapCoord(x, size.x, int(programState.z)), wrapCoord(y, size.y, int(programState.w))), 0);
}
void main()
{
    vec4 c;
    if (programState.x == 2.0) c = colour;
    else {
        if (programState.y != 0.0) {
            // Match CPU RC float interpolation before modulation and alpha test.
            vec2 p = uv - vec2(0.5);
            ivec2 i = ivec2(floor(p));
            vec2 w = fract(p);
            vec4 a = fetchWrapped(i.x, i.y);
            vec4 b = fetchWrapped(i.x + 1, i.y);
            vec4 d = fetchWrapped(i.x, i.y + 1);
            vec4 e = fetchWrapped(i.x + 1, i.y + 1);
            c = (a * (1.0 - w.x) + b * w.x) * (1.0 - w.y)
              + (d * (1.0 - w.x) + e * w.x) * w.y;
        } else if (programState.z != 3.0 || programState.w != 3.0) {
            ivec2 i = ivec2(floor(uv));
            c = fetchWrapped(i.x, i.y);
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
