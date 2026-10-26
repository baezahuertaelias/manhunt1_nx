#version 460

// Same triangle as devkitPro deko_basic: covers half of the render target.
const vec4 positions[3] = vec4[](
    vec4( 0.0, +1.0, 0.0, 1.0),
    vec4(-1.0, -1.0, 0.0, 1.0),
    vec4(+1.0, -1.0, 0.0, 1.0)
);

void main()
{
    gl_Position = positions[gl_VertexID];
}
