#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef HZ_PLATFORM_SWITCH
#include <switch.h>
#endif
static int seek_failure, write_failure, short_write, write_calls;
static off_t checked_seek(int fd, off_t off, int whence)
{
    if (seek_failure) { errno = seek_failure == 1 ? EIO : 0; return -1; }
    return lseek(fd, off, whence);
}
static ssize_t checked_write(int fd, const void *data, size_t size)
{
    write_calls++;
    if (write_failure) { errno = write_failure == 1 ? ENOSPC : 0; return -1; }
    return write(fd, data, short_write ? size - 1 : size);
}
#define lseek checked_seek
#define write checked_write
#define XBOX_PART_TABLE_OFFSET 0x800
#include "partition-helper.h"
#undef lseek
#undef write
static unsigned checks;
static void report(const char *message)
{
    puts(message);
#ifdef HZ_PLATFORM_SWITCH
    svcOutputDebugString(message, strlen(message));
#endif
}
static int failed(unsigned line, const char *condition)
{
    char message[256]; snprintf(message, sizeof message, "HZ-PARTITION-PROOF FAIL line=%u %s", line, condition);
    report(message); return 1;
}
#define CHECK(c) do { checks++; if (!(c)) return failed(__LINE__, #c); } while (0)
int main(void)
{
#ifdef HZ_PLATFORM_SWITCH
    char path[128]; snprintf(path, sizeof path, "sdmc:/manhunt-partition-proof-v3-%lu.tmp", (unsigned long)getpid());
#else
    char path[] = "/tmp/manhunt-partition-XXXXXX";
    int temp = mkstemp(path); CHECK(temp >= 0); CHECK(close(temp) == 0); CHECK(unlink(path) == 0);
#endif
    uint8_t sector[512], actual[4096], before[4096];
    memset(sector, 0x69, sizeof sector); memset(before, 0x37, sizeof before);
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600); CHECK(fd >= 0);
    CHECK(write_partition_sector(fd, sector) == 512);
    CHECK(lseek(fd, 0, SEEK_END) == 0xA00);
    CHECK(lseek(fd, 0x800, SEEK_SET) == 0x800);
    CHECK(read(fd, actual, 512) == 512 && !memcmp(actual, sector, 512));
    CHECK(close(fd) == 0);
    fd = open(path, O_RDWR | O_TRUNC, 0600); CHECK(fd >= 0);
    CHECK(write(fd, before, sizeof before) == sizeof before);
    CHECK(lseek(fd, 15, SEEK_SET) == 15);
    CHECK(write_partition_sector(fd, sector) == 512);
    CHECK(lseek(fd, 0, SEEK_END) == sizeof before);
    CHECK(lseek(fd, 0, SEEK_SET) == 0);
    CHECK(read(fd, actual, sizeof actual) == sizeof actual);
    CHECK(!memcmp(actual, before, 0x800));
    CHECK(!memcmp(actual + 0x800, sector, sizeof sector));
    CHECK(!memcmp(actual + 0xA00, before + 0xA00, sizeof actual - 0xA00));
    int prior = write_calls;
    seek_failure = 1; errno = 0;
    CHECK(write_partition_sector(fd, sector) == -1 && errno == EIO && write_calls == prior);
    seek_failure = 2; errno = 0;
    CHECK(write_partition_sector(fd, sector) == -1 && errno == EIO && write_calls == prior);
    seek_failure = 0; write_failure = 1; errno = 0;
    CHECK(write_partition_sector(fd, sector) == -1 && errno == ENOSPC);
    write_failure = 2; errno = 0;
    CHECK(write_partition_sector(fd, sector) == -1 && errno == EIO);
    write_failure = 0; short_write = 1;
    CHECK(write_partition_sector(fd, sector) == 511 && errno == EIO);
    short_write = 0;
    CHECK(close(fd) == 0);
    errno = 0; off_t direct = lseek(-1, XBOX_PART_TABLE_OFFSET, SEEK_SET); int direct_errno = errno;
    prior = write_calls; errno = 0;
    ssize_t invalid = write_partition_sector(-1, sector); int helper_errno = errno;
    char diagnostic[192]; snprintf(diagnostic, sizeof diagnostic,
        "HZ-PARTITION-INVALID direct=%ld errno=%d helper=%ld errno=%d writes=%d",
        (long)direct, direct_errno, (long)invalid, helper_errno, write_calls - prior);
    report(diagnostic);
    CHECK(direct == -1);
    CHECK(invalid == -1 && helper_errno == (direct_errno ? direct_errno : EIO) && write_calls == prior);
    CHECK(unlink(path) == 0);
    char message[128]; snprintf(message, sizeof message, "HZ-PARTITION-PROOF PASS checks=%u", checks);
    report(message);
    return 0;
}
