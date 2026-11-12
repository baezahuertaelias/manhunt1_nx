/* Production ring and packed-upload contracts, using the review fake driver. */
#define main review_base_main
#include "test_gl.c"
#undef main

typedef struct { uint8_t *data; size_t size; int immutable; } StreamBuffer;
static StreamBuffer stream_buffer[2048];
static GLuint stream_bound[2];
static unsigned stream_maps, stream_unmaps, stream_subdata, stream_stores, stream_fences, stream_waits, stream_deletes;
static int stream_fail_map, stream_fail_fence, stream_fail_wait;
static unsigned stream_target(GLenum target) { assert(target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER); return target == GL_ELEMENT_ARRAY_BUFFER; }
static StreamBuffer *stream_current(GLenum target) { return &stream_buffer[stream_bound[stream_target(target)]]; }
static void stream_bind(GLenum target, GLuint id) { stream_bound[stream_target(target)] = id; }
static void stream_delete(GLsizei n, const GLuint *ids) { while (n--) { StreamBuffer *b=&stream_buffer[*ids++]; free(b->data); memset(b,0,sizeof *b); } }
static void stream_data(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    (void)usage; StreamBuffer *b=stream_current(target); assert(!b->immutable);
    free(b->data); b->data=calloc(1,(size_t)size); assert(b->data); b->size=(size_t)size;
    if(data)memcpy(b->data,data,(size_t)size);
}
static void stream_storage(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags)
{
    assert(flags==(GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT));
    stream_data(target,size,data,0); stream_current(target)->immutable=1; stream_stores++;
}
static void *stream_map(GLenum target, GLintptr at, GLsizeiptr size, GLbitfield flags)
{
    StreamBuffer *b=stream_current(target); stream_maps++;
    assert(at>=0 && size>0 && (size_t)(at+size)<=b->size);
    assert(!(flags&GL_MAP_INVALIDATE_RANGE_BIT));
    if(b->immutable)assert(flags==(GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT));
    if(stream_fail_map)return NULL;
    return b->data+at;
}
static GLboolean stream_unmap(GLenum target) { (void)target; stream_unmaps++; return GL_TRUE; }
static void stream_put(GLenum target, GLintptr at, GLsizeiptr size, const void *data)
{ StreamBuffer *b=stream_current(target); assert(!b->immutable && (size_t)(at+size)<=b->size); memcpy(b->data+at,data,(size_t)size); stream_subdata++; }
static GLsync stream_fence(GLenum condition, GLbitfield flags)
{ assert(condition==GL_SYNC_GPU_COMMANDS_COMPLETE && !flags); stream_fences++; return stream_fail_fence?NULL:(GLsync)(uintptr_t)stream_fences; }
static GLenum stream_wait(GLsync fence, GLbitfield flags, uint64_t timeout)
{ assert(fence && flags==GL_SYNC_FLUSH_COMMANDS_BIT && timeout>0); stream_waits++; return stream_fail_wait?GL_WAIT_FAILED:GL_CONDITION_SATISFIED; }
static void stream_delete_sync(GLsync fence) { assert(fence); stream_deletes++; }
static void init_stream(void)
{
    init_fake(); p_glGenBuffers=gen; p_glBindBuffer=stream_bind; p_glDeleteBuffers=stream_delete;
    p_glBufferData=stream_data; p_glBufferStorage=stream_storage; p_glMapBufferRange=stream_map;
    p_glUnmapBuffer=stream_unmap; p_glBufferSubData=stream_put;
    p_glFenceSync=stream_fence; p_glClientWaitSync=stream_wait; p_glDeleteSync=stream_delete_sync;
}
static GlRing new_ring(GLsizeiptr cap, int persistent)
{ GlRing g={.target=GL_ARRAY_BUFFER,.cap=cap}; glGenBuffers(1,&g.buf); ring_init(&g,persistent); return g; }
static void put(GlRing *g, GLsizeiptr n, uint8_t value)
{ void *ptr; GLintptr at=ring_alloc(g,n,&ptr); assert(ptr); memset(ptr,value,(size_t)n); ring_done(g,at,n,ptr,NULL); }
static void persistent_contract(void)
{
    unsigned maps=stream_maps, unmaps=stream_unmaps, waits=stream_waits;
    GlRing g=new_ring(192,1); assert(g.mapped && stream_maps==maps+1 && g.segment_size==64);
    put(&g,64,0x11); ring_flip(&g); assert(g.segment==1 && stream_waits==waits);
    put(&g,64,0x22); ring_flip(&g); assert(g.segment==2 && stream_waits==waits);
    ring_flip(&g); assert(g.segment==2); /* empty segment cannot fence or advance */
    put(&g,64,0x33); ring_flip(&g); assert(g.segment==0 && stream_waits==waits+1);
    assert(g.mapped[0]==0x11 && g.mapped[64]==0x22 && g.mapped[128]==0x33);
    put(&g,64,0x44); put(&g,64,0x55); /* mid-frame exhaustion fences, then waits for seg1 */
    assert(g.segment==1 && stream_waits==waits+2 && stream_maps==maps+1 && stream_unmaps==unmaps);
    assert(g.mapped[0]==0x44 && g.mapped[64]==0x55);
    put(&g,256,0x66); /* oversized draw drains and replaces immutable storage */
    assert(g.segment_size>=256 && g.mapped && stream_maps==maps+2 && stream_unmaps==unmaps+1);
    assert(g.mapped[0]==0x66);
    puts("PASS persistent single map, three fenced segments, no wait before reuse, mid-frame wrap and oversized draw");
}
static void mutable_contract(void)
{
    unsigned stores=stream_stores, maps=stream_maps, unmaps=stream_unmaps, puts_before=stream_subdata;
    GlRing g=new_ring(128,0); assert(!g.mapped && stream_stores==stores);
    put(&g,80,0x11); put(&g,80,0x22); assert(g.off==80); /* mutable orphan on wrap */
    assert(stream_maps==maps+2 && stream_unmaps==unmaps+2 && stream_buffer[g.buf].data[0]==0x22);
    stream_fail_map=1; void *ptr; GLintptr at=ring_alloc(&g,4,&ptr); assert(!ptr);
    uint32_t word=0x12345678; ring_done(&g,at,4,NULL,&word); stream_fail_map=0;
    assert(stream_subdata==puts_before+1 && !memcmp(stream_buffer[g.buf].data+at,&word,4));
    stream_fail_map=1; g=new_ring(192,1); stream_fail_map=0;
    assert(!g.mapped && !stream_buffer[g.buf].immutable); put(&g,4,0x77);
    puts("PASS mutable fallback wrap, failed-map subdata and failed persistent-map buffer replacement");
}
static void packed_contract(void)
{
    const uint32_t indices[]={2,0,1}; float attrs[3*NV2A_RAW_ATTRS*4]={0};
    float positions[3][4]={{1,2,3,1},{4,5,6,1},{7,8,9,1}};
    Nv2aRawBatch b={0}; b.vertex_count=3;b.attr_present=1;b.attr_direct=1;
    b.direct[0].ptr=(const uint8_t *)positions;b.direct[0].type=2;b.direct[0].size=4;b.direct[0].stride=16;b.attrs=attrs;
    s_streaming=1;s_vring=new_ring(1024,0);unsigned maps=stream_maps,unmaps=stream_unmaps;
    GLintptr at; assert(upload_vertices(&b,indices,3,GL_UNSIGNED_SHORT,&at));
    assert(stream_maps==maps+1 && stream_unmaps==unmaps+1 && stream_bound[1]==s_vring.buf);
    StreamBuffer *buf=&stream_buffer[s_vring.buf];const uint16_t expect16[]={2,0,1};
    assert(!memcmp(buf->data,positions,sizeof positions) && !memcmp(buf->data+at,expect16,sizeof expect16));
    stream_fail_map=1; assert(upload_vertices(&b,indices,3,GL_UNSIGNED_INT,&at));stream_fail_map=0;
    assert(!memcmp(buf->data+at,indices,sizeof indices));
    s_vring=new_ring(1024,1);maps=stream_maps;unmaps=stream_unmaps;
    assert(upload_vertices(&b,indices,3,GL_UNSIGNED_SHORT,&at));
    assert(stream_maps==maps && stream_unmaps==unmaps && !memcmp(s_vring.mapped+at,expect16,sizeof expect16));
    puts("PASS one packed vertex/index map, 16/32-bit indices, map-failure staging and persistent no-map uploads");
}
static void bad_fence(void) { GlRing g=new_ring(192,1);put(&g,4,1);stream_fail_fence=1;ring_flip(&g); }
static void bad_wait(void) { GlRing g=new_ring(192,1);for(int i=0;i<3;i++){put(&g,4,1);if(i==2)stream_fail_wait=1;ring_flip(&g);} }
int main(void)
{
    setenv("MANHUNT_PERF_LOG","2",1); init_stream();
    persistent_contract();mutable_contract();packed_contract();expect_abort(bad_fence);expect_abort(bad_wait);
    puts("PASS persistent fence allocation/wait errors stop before unsafe reuse");return 0;
}
