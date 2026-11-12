/* Disk-format rollover must not strand new records behind an obsolete ABI. */
#define main renderer_review_main
#include "test_gl.c"
#undef main
#include <sys/stat.h>
static void write_prefix(const char *path, size_t bytes, uint32_t magic)
{
    ProgRec rec = {0}; rec.magic = magic; rec.xform = 1;
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(&rec, 1, bytes, f) == bytes); assert(!fclose(f));
}
static long cache_bytes(const char *path)
{
    FILE *f = fopen(path, "rb"); assert(f); assert(!fseek(f, 0, SEEK_END));
    long bytes = ftell(f); assert(!fclose(f)); return bytes;
}
static void append_and_check(const char *path)
{
    Nv2aRawBatch b = {0}; Nv2aPshKey pk = {0}; b.xform = 1;
    prog_cache_append(&b, &pk); assert(cache_bytes(path) == sizeof(ProgRec));
    ProgRec rec; FILE *f = fopen(path, "rb"); assert(f);
    assert(fread(&rec, sizeof rec, 1, f) == 1); assert(!fclose(f));
    assert(rec.magic == PROG_REC_MAGIC && rec.xform == 1);
    prog_cache_prewarm(); /* Valid ABI is retained even if mock compilation fails. */
    assert(cache_bytes(path) == sizeof(ProgRec));
}
int main(void)
{
    char path[] = "/tmp/manhunt-progcache-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    init_fake(); p_glCreateShader = make_shader; p_glGetShaderiv = status_bad; p_glGetShaderInfoLog = info_log;
    setenv("RECOMP_PROG_CACHE", path, 1);
    write_prefix(path, sizeof(ProgRec), 0x4350564e);
    prog_cache_prewarm(); assert(cache_bytes(path) == 0); append_and_check(path);
    write_prefix(path, sizeof(ProgRec) - 7, PROG_REC_MAGIC);
    prog_cache_prewarm(); assert(cache_bytes(path) == 0); append_and_check(path);
    /* A short tail after a good record must also allow future warmups. */
    FILE *f = fopen(path, "ab"); assert(f); assert(fwrite("bad", 1, 3, f) == 3); fclose(f);
    prog_cache_prewarm(); assert(cache_bytes(path) == 0); append_and_check(path);
    /* Write failures remain optional-cache failures, and stop further appends. */
    setenv("RECOMP_PROG_CACHE", "/dev/full", 1);
    Nv2aRawBatch b = {0}; Nv2aPshKey pk = {0}; prog_cache_append(&b, &pk);
    assert(s_prog_cache_write_disabled);
    setenv("RECOMP_PROG_CACHE", path, 1); prog_cache_append(&b, &pk);
    assert(cache_bytes(path) == sizeof(ProgRec));
    assert(!unlink(path));
    puts("PASS shader cache obsolete ABI, truncated record/tail rollover and graceful write failure");
    return 0;
}
