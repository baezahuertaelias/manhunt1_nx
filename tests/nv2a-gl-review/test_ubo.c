/* Exercise the production UBO cache and orphan ownership contracts. */
#define main renderer_review_main
#include "test_gl.c"
#undef main
static unsigned ubo_uploads, ubo_orphans, ubo_binds;
static GLint fake_alignment = 256;
static unsigned char ubo_storage[GL_UBO_CAP];
static GLintptr ubo_at[2];
static GLsizeiptr ubo_size[2];
static void ubo_integer(GLenum name, GLint *value)
{ assert(name == GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT); *value = fake_alignment; }
static void ubo_bind(GLenum target, GLuint name)
{ assert(target == GL_UNIFORM_BUFFER); assert(name); }
static void ubo_data(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    assert(target == GL_UNIFORM_BUFFER && size == GL_UBO_CAP && !data && usage == GL_STREAM_DRAW);
    memset(ubo_storage, 0xdd, sizeof ubo_storage); ubo_orphans++;
}
static void ubo_subdata(GLenum target, GLintptr at, GLsizeiptr size, const void *data)
{
    assert(target == GL_UNIFORM_BUFFER && at >= 0 && at + size <= GL_UBO_CAP);
    memcpy(ubo_storage + at, data, (size_t)size); ubo_uploads++;
}
static void ubo_range(GLenum target, GLuint index, GLuint buffer, GLintptr at, GLsizeiptr size)
{
    assert(target == GL_UNIFORM_BUFFER && index < 2 && buffer == s_ubo.buf);
    assert(at % fake_alignment == 0); ubo_at[index] = at; ubo_size[index] = size; ubo_binds++;
}
static void invalid_alignment(void)
{
    Nv2aRawBatch b = {0}; s_ubo.buf = 0; fake_alignment = 0; uniform_blocks(&b, 1);
}
int main(void)
{
    setenv("MANHUNT_GL_UBO", "0", 1); assert(!gl_ubo());
    init_fake();
    p_glGetIntegerv = ubo_integer; p_glGenBuffers = gen;
    p_glBindBuffer = ubo_bind; p_glBufferData = ubo_data;
    p_glBufferSubData = ubo_subdata; p_glBindBufferRange = ubo_range;
    float constants[192][4]; uint32_t regs[0x2000 / 4] = {0};
    for (unsigned i = 0; i < 192; ++i) for (unsigned k = 0; k < 4; ++k) constants[i][k] = i + k * 0.25f;
    Nv2aRawBatch b = {0}; b.vp_consts = constants; b.vp_const_gen = 1; b.regs = regs;
    uniform_blocks(&b, 0); assert(!ubo_uploads && !ubo_orphans);
    uniform_blocks(&b, 3); assert(ubo_uploads == 2 && ubo_orphans == 1 && ubo_binds == 2);
    assert(ubo_size[0] == sizeof constants && ubo_size[1] == 46 * 16);
    assert(!memcmp(ubo_storage + ubo_at[0], constants, sizeof constants));
    uniform_blocks(&b, 3); assert(ubo_uploads == 2 && ubo_binds == 2);
    /* Switching shader programs doesn't change these context-wide bindings. */
    uniform_blocks(&b, 1); uniform_blocks(&b, 2); assert(ubo_uploads == 2);
    constants[191][3] = -19.25f; b.vp_const_gen++;
    uniform_blocks(&b, 3); assert(ubo_uploads == 3 && ubo_binds == 3);
    assert(!memcmp(ubo_storage + ubo_at[0], constants, sizeof constants));
    b.lighting = 1; regs[0x314 / 4] = 1;
    uniform_blocks(&b, 3); assert(ubo_uploads == 4);
    s_ubo.off = GL_UBO_CAP - 64;
    uniform_blocks(&b, 3); assert(ubo_uploads == 6 && ubo_orphans == 2);
    assert(ubo_at[0] + ubo_size[0] <= ubo_at[1]);
    assert(!memcmp(ubo_storage + ubo_at[0], constants, sizeof constants));
    /* Wrapping during a lighting-only draw invalidates constants too; the
     * next programmable draw must reupload even at an unchanged generation. */
    s_ubo.off = GL_UBO_CAP - 64;
    uniform_blocks(&b, 2); assert(ubo_uploads == 7 && !s_ubo.const_valid && ubo_orphans == 3);
    uniform_blocks(&b, 1); assert(ubo_uploads == 8 && s_ubo.const_valid);
    assert(ubo_at[1] + ubo_size[1] <= ubo_at[0]);
    assert(!memcmp(ubo_storage + ubo_at[0], constants, sizeof constants));
    expect_abort(invalid_alignment);
    puts("PASS production UBO generations, cached bindings, std140 alignment, orphan lifetime and cross-program wrap");
    return 0;
}
