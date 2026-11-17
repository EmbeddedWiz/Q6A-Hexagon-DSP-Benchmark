/*
 * Simple memory-bandwidth load for the RT measurement conditions.
 * Two threads pinned to CPUs 4 and 5 stream read/write traffic over a
 * 256 MiB buffer per thread. The process runs until it is terminated.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_BYTES (256ULL * 1024ULL * 1024ULL)
#define WORDS (BUFFER_BYTES / sizeof(uint64_t))

static volatile uint64_t sink;

static void *worker(void *arg)
{
    const int cpu = (int)(intptr_t)arg;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
        perror("pthread_setaffinity_np");
        return NULL;
    }

    uint64_t *buffer = (uint64_t *)malloc(BUFFER_BYTES);
    if (buffer == NULL) {
        perror("malloc");
        return NULL;
    }
    for (size_t i = 0; i < WORDS; ++i) {
        buffer[i] = (uint64_t)i * 0x9e3779b97f4a7c15ULL;
    }

    uint64_t local = 0;
    for (;;) {
        for (size_t i = 0; i < WORDS; i += 8) {
            local += buffer[i];
            buffer[i] ^= local;
            local += buffer[i + 1];
            buffer[i + 1] ^= local;
            local += buffer[i + 2];
            buffer[i + 2] ^= local;
            local += buffer[i + 3];
            buffer[i + 3] ^= local;
        }
        sink = local;
    }
    return NULL;
}

int main(void)
{
    pthread_t threads[2];
    for (int i = 0; i < 2; ++i) {
        if (pthread_create(&threads[i], NULL, worker,
                           (void *)(intptr_t)(4 + i)) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    for (int i = 0; i < 2; ++i) {
        pthread_join(threads[i], NULL);
    }
    return 0;
}
