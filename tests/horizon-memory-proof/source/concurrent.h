/* Pause view creation under its registry lock and race the last handle close. */
#include <time.h>
static _Thread_local int test_actor;
static pthread_mutex_t test_control = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_condition = PTHREAD_COND_INITIALIZER;
static int test_create_entered, test_close_entered, test_continue;
static HANDLE test_mapping;
static void *test_view;
static void test_wait(int *condition)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline); deadline.tv_sec += 5;
    while (!*condition)
        if (pthread_cond_timedwait(&test_condition, &test_control, &deadline)) abort();
}
static void test_pause_create(void)
{
    if (test_actor != 1) return;
    pthread_mutex_lock(&test_control);
    test_create_entered = 1; pthread_cond_broadcast(&test_condition);
    test_wait(&test_continue);
    pthread_mutex_unlock(&test_control);
}
static int test_registry_lock(pthread_mutex_t *lock)
{
    if (test_actor == 2) {
        pthread_mutex_lock(&test_control);
        test_close_entered = 1; pthread_cond_broadcast(&test_condition);
        test_wait(&test_continue);
        pthread_mutex_unlock(&test_control);
    }
    return pthread_mutex_lock(lock);
}
static void *test_map_thread(void *unused)
{
    (void)unused; test_actor = 1;
    test_view = MapViewOfFile(test_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    return NULL;
}
static void *test_close_thread(void *unused)
{
    (void)unused; test_actor = 2;
    CloseHandle(test_mapping);
    return NULL;
}
static int test_close_race(void)
{
    pthread_t mapper, closer;
    test_create_entered = test_close_entered = test_continue = 0; test_view = NULL;
    test_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 8192, NULL);
    if (!test_mapping || pthread_create(&mapper, NULL, test_map_thread, NULL)) return 0;
    pthread_mutex_lock(&test_control);
    test_wait(&test_create_entered);
    if (pthread_create(&closer, NULL, test_close_thread, NULL)) abort();
    test_wait(&test_close_entered);
    /* Closing cannot consume the final handle reference while publication owns the lock. */
    if (__atomic_load_n(&((w32_object *)test_mapping)->refcount, __ATOMIC_SEQ_CST) != 1) {
        fprintf(stderr, "FAIL close-before-publication consumed a reference outside the registry lock\n");
        abort();
    }
    test_continue = 1; pthread_cond_broadcast(&test_condition);
    pthread_mutex_unlock(&test_control);
    pthread_join(mapper, NULL); pthread_join(closer, NULL);
    if (!test_view) return 0;
    ((unsigned char *)test_view)[0] = 0x63;
    return UnmapViewOfFile(test_view);
}
