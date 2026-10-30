#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

extern int manhunt_test_posix_memalign(void **, size_t, size_t);
static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main(void)
{
    const size_t invalid[] = {0, 1, 3, sizeof(void *) / 2, sizeof(void *) * 3};
    void *sentinel = (void *)(uintptr_t)0x1234;
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        void *p = sentinel;
        errno = EAGAIN;
        CHECK(manhunt_test_posix_memalign(&p, invalid[i], 23) == EINVAL);
        CHECK(p == sentinel && errno == EAGAIN);
    }
    void *p = sentinel;
    CHECK(manhunt_test_posix_memalign(&p, 4096, SIZE_MAX) == ENOMEM);
    CHECK(p == sentinel);
    const size_t alignments[] = {sizeof(void *), 16, 4096};
    const size_t sizes[] = {0, 1, 23, 8193};
    for (size_t a = 0; a < sizeof alignments / sizeof alignments[0]; a++)
        for (size_t n = 0; n < sizeof sizes / sizeof sizes[0]; n++) {
            p = sentinel;
            errno = EAGAIN;
            CHECK(manhunt_test_posix_memalign(&p, alignments[a], sizes[n]) == 0);
            CHECK(errno == EAGAIN);
            CHECK(!p || (uintptr_t)p % alignments[a] == 0);
            CHECK(p || !sizes[n]);
            if (p && sizes[n]) {
                memset(p, 0xA5, sizes[n]);
                CHECK(((unsigned char *)p)[sizes[n] - 1] == 0xA5);
            }
            free(p);
        }
    char result[128];
    snprintf(result, sizeof result, "HORIZON-ALLOC-PROOF %s checks=%u failures=%u", failures ? "FAIL" : "PASS", checks, failures);
    puts(result);
#ifdef __SWITCH__
    svcOutputDebugString(result, strlen(result));
#endif
    return failures != 0;
}
