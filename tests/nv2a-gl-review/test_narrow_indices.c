/* Executor-narrowed 16-bit indices (Nv2aRawBatch.indices16, patch 0038,
 * MANHUNT_NARROW_INDICES) give GL the same element data as 32-bit ones:
 * gl_prim (QUADS rewrite included), the packed streaming upload (mapped and
 * map-failure staging), the GL-thread queue record, and the back-end flag. */
#define STREAMING_NO_MAIN
#include "test_streaming.c"

#define NV_SMALL 300u
#define NV_BIG   65536u
#define NIDX     1200u

static uint32_t idx32[NIDX];
static uint16_t idx16[NIDX];
static float pos[NV_BIG][4];

static void make_indices(uint32_t nv)
{
    uint32_t s = 12345;
    for (unsigned i = 0; i < NIDX; i++) {
        s = s * 1103515245u + 12345u;
        idx32[i] = i < 2 ? (i ? nv - 1 : 0) : (s >> 8) % nv;    /* both ends of the range */
        idx16[i] = (uint16_t)idx32[i];                           /* what raw_batch writes */
    }
}

static void batch(Nv2aRawBatch *b, uint32_t prim, uint32_t nv, int narrow)
{
    memset(b, 0, sizeof *b);
    b->prim = prim; b->vertex_count = nv; b->index_count = NIDX;
    if (narrow) b->indices16 = idx16; else b->indices = idx32;
    b->attr_present = b->attr_direct = 1;
    b->direct[0].ptr = (const uint8_t *)pos; b->direct[0].type = 2; b->direct[0].size = 4; b->direct[0].stride = 16;
}

/* GL element data for one batch: (count, type, bytes). */
static size_t gl_elements(const Nv2aRawBatch *b, uint8_t *out, uint32_t *count, GLenum *type)
{
    const void *idx; uint32_t n; GLintptr at; GLint bv;
    gl_prim(b, &idx, &n);
    *type = b->indices16 || b->vertex_count <= 65536 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    assert(upload_vertices(b, idx, n, *type, &at, &bv));
    size_t bytes = (size_t)n * (*type == GL_UNSIGNED_SHORT ? 2 : 4);
    const uint8_t *src = s_vring.mapped ? s_vring.mapped + at : stream_buffer[s_vring.buf].data + at;
    memcpy(out, src, bytes);
    *count = n;
    return bytes;
}

static uint8_t e32[NIDX * 8], e16[NIDX * 8];

static void same_elements(uint32_t prim, uint32_t nv, int persistent, int fail_map)
{
    Nv2aRawBatch b32, b16; uint32_t n32, n16; GLenum t32, t16;
    make_indices(nv);
    s_streaming = 1; s_vring = new_ring(8u << 20, persistent); s_kept.valid = 0;
    stream_fail_map = fail_map;
    batch(&b32, prim, nv, 0); batch(&b16, prim, nv, 1);
    size_t s32 = gl_elements(&b32, e32, &n32, &t32);
    size_t s16 = gl_elements(&b16, e16, &n16, &t16);
    stream_fail_map = 0;
    assert(n32 == n16 && t32 == t16 && t16 == GL_UNSIGNED_SHORT && s32 == s16);
    assert(!memcmp(e32, e16, s32));
    if (prim == 8) assert(n16 == NIDX / 4 * 6);
}

static void queue_record(uint32_t nv)
{
    Nv2aRawBatch b32, b16, d;
    make_indices(nv);
    batch(&b32, 5, nv, 0); batch(&b16, 5, nv, 1);
    s_q_head = s_q_tail = 0; s_q_npend = 0;
    memset(s_q_regs, 0, sizeof s_q_regs); memset(nv2a_pb_reg_dirty(), 0, 128);
    uint32_t regs[2048] = {0}; b32.regs = b16.regs = regs;
    q_draw(&b32);
    uint32_t size32 = ((const QHdr *)s_q)->size;
    s_q_head = s_q_tail = 0; s_q_npend = 0;
    memset(s_q_regs, 0, sizeof s_q_regs); memset(nv2a_pb_reg_dirty(), 0, 128);
    q_draw(&b16);
    const QHdr *h = (const QHdr *)s_q;
    assert(h->type == Q_DRAW);
    size_t batch_at = sizeof(QHdr) + h->nreg * sizeof(QRegChunk) + (h->nprog + h->nconst) * sizeof(QRow);
    q_read_batch(s_q, batch_at, &d);
    assert(!d.indices && d.indices16 && d.index_count == NIDX);
    assert(!memcmp(d.indices16, idx16, sizeof idx16));
    assert(size32 - h->size == NIDX * 2);            /* half the index bytes through the queue */
}

static void install_flag(void)
{
    unsetenv("RECOMP_GL_THREAD");
    unsetenv("MANHUNT_NARROW_INDICES"); nv2a_gl_install();
    assert(registered_backend && (registered_backend->flags & NV2A_BACKEND_RAW_INDEX16));
    setenv("MANHUNT_NARROW_INDICES", "0", 1); nv2a_gl_install();
    assert(!(registered_backend->flags & NV2A_BACKEND_RAW_INDEX16));
    unsetenv("MANHUNT_NARROW_INDICES");
}

int main(void)
{
    static const uint32_t prims[] = { 1, 2, 5, 6, 7, 8, 9, 10 };
    setenv("MANHUNT_GL_SYNC", "gpu", 1); setenv("MANHUNT_PERF_LOG", "2", 1); init_stream();
    for (uint32_t v = 0; v < NV_BIG; v++) { pos[v][0] = (float)v; pos[v][3] = 1; }
    for (unsigned p = 0; p < sizeof prims / sizeof prims[0]; p++) {
        same_elements(prims[p], NV_SMALL, 1, 0);
        same_elements(prims[p], NV_BIG, 1, 0);
        same_elements(prims[p], NV_SMALL, 0, 0);
        same_elements(prims[p], NV_BIG, 0, 1);
    }
    puts("PASS narrowed indices: identical GL element data for every primitive (QUADS rewrite too),"
         " 300 and 65536 vertices, persistent/mutable/staging uploads");
    s_q = calloc(1, Q_BYTES); assert(s_q);
    queue_record(NV_SMALL); queue_record(NV_BIG);
    puts("PASS narrowed indices: queue record carries 16-bit indices, half the bytes, relocated intact");
    install_flag();
    puts("PASS MANHUNT_NARROW_INDICES: back end asks for 16-bit indices by default, =0 keeps 32-bit");
    free(s_q);
    return 0;
}
