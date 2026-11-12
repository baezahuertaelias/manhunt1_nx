/* Exercise real producer records and consumer relocation, without a GL thread. */
#define main renderer_review_main
#include "test_gl.c"
#undef main
static void contract(uint16_t present,uint16_t direct,int wrap)
{
    const unsigned nv=5;float attrs[5*64];uint32_t regs[2048]={0},indices[3]={0,2,4};
    uint32_t words[5][4];
    for(unsigned v=0;v<nv;v++)for(unsigned a=0;a<16;a++)for(unsigned c=0;c<4;c++) {
        uint32_t bits=0x3f000000u+v*4096+a*32+c;
        if(v==3&&a==2&&c==1)bits=0xffc00001u; /* preserve NaN payload */
        memcpy(&attrs[v*64+a*4+c],&bits,4);
    }
    for(unsigned v=0;v<nv;v++)for(unsigned c=0;c<4;c++)words[v][c]=v*16+c;
    Nv2aRawBatch b={0};b.vertex_count=nv;b.index_count=3;b.indices=indices;b.attrs=attrs;b.regs=regs;
    b.attr_present=present;b.attr_direct=direct;
    for(unsigned a=0;a<16;a++)if(direct&(1u<<a)) {
        b.direct[a].ptr=(const uint8_t *)words;b.direct[a].type=2;b.direct[a].size=4;b.direct[a].stride=16;
    }
    s_q_head=s_q_tail=wrap?Q_BYTES-16:0;s_q_npend=0;
    memset(s_q_regs,0,sizeof s_q_regs);memset(nv2a_pb_reg_dirty(),0,128);
    uint64_t before=s_pf.converted_verts,bytes=s_pf.queue_attr_bytes,saved=s_pf.queue_attr_saved_bytes;
    q_draw(&b);
    const uint8_t *rec=s_q;const QHdr *h=(const QHdr *)rec;
    assert(h->type==Q_DRAW);assert(h->size<=Q_BYTES/4);
    if(wrap){const QHdr *padding=(const QHdr *)(s_q+Q_BYTES-16);assert(padding->type==Q_WRAP&&padding->size==16);}
    size_t batch_at=sizeof(QHdr)+h->nreg*sizeof(QRegChunk)+(h->nprog+h->nconst)*sizeof(QRow);
    Nv2aRawBatch decoded;q_read_batch(rec,batch_at,&decoded);
    unsigned nconv=0;uint16_t conv=present&~direct;
    for(unsigned a=0;a<16;a++)if(conv&(1u<<a))nconv++;
    assert(!memcmp(decoded.indices,indices,sizeof indices));
    for(unsigned a=0;a<16;a++) {
        if(direct&(1u<<a)) {assert(decoded.direct[a].type==2);assert(!memcmp(decoded.direct[a].ptr,words,sizeof words));}
        if(!(conv&(1u<<a)))continue;
        if(queue_pack()) {
            assert(decoded.attr_direct&(1u<<a));assert(decoded.direct[a].type==2&&decoded.direct[a].size==4);
            assert(decoded.direct[a].stride==nconv*16);
            for(unsigned v=0;v<nv;v++)assert(!memcmp(decoded.direct[a].ptr+v*decoded.direct[a].stride,attrs+v*64+a*4,16));
        } else for(unsigned v=0;v<nv;v++)assert(!memcmp(decoded.attrs+v*64+a*4,attrs+v*64+a*4,16));
    }
    assert(decoded.attr_direct==(queue_pack()?present:direct));
    if(queue_pack()||!conv)assert(!decoded.attrs);
    if(conv) {
        size_t expected=(size_t)nv*(queue_pack()?nconv:16)*16;
        assert(s_pf.queue_attr_bytes-bytes==expected);
        assert(s_pf.queue_attr_saved_bytes-saved==nv*256-expected);
        if(queue_pack())assert(s_pf.converted_verts-before==nv*nconv);
    } else assert(s_pf.queue_attr_bytes==bytes);
}
int main(void)
{
    setenv("MANHUNT_GL_SYNC","gpu",1);setenv("MANHUNT_PERF_LOG","2",1);init_fake();
    s_q=calloc(1,Q_BYTES);assert(s_q);
    contract(5,1,0);contract(5,1,1); /* one converted CMP normal plus direct position */
    contract(0x800b,2,0);contract(0xffff,0,0);contract(5,5,0);contract(0,0,0);
    puts("PASS converted queue packing: sparse/full masks, direct coexistence, exact float/NaN bits, ring wrap, relocation and byte/conversion counters");
    free(s_q);return 0;
}
