#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

int posix_memalign(void **out, size_t align, size_t size)
{
    if (align < sizeof(void *) || align % sizeof(void *) || (align & (align - 1)))
        return EINVAL;
    if (size > SIZE_MAX - (align - 1))
        return ENOMEM;
    int saved_errno = errno;
    void *p = aligned_alloc(align, (size + align - 1) & ~(align - 1));
    errno = saved_errno;
    if (!p && size)
        return ENOMEM;
    *out = p;
    return 0;
}
