#include <immintrin.h>
#include <stdint.h>
#include <sys/mman.h>
#include "bitmap.h"
#include <stdio.h>
#include <string.h>

#define LARGE_SHARD_SIZE 256
#define LARGE_SHARD_SEC (1024 * 1024)
#define SMALL_SHARD_SIZE 48
#define SMALL_SHARD_SEC (1024 * 1024 * 3)
#define LARGE_CLUSTERS (LARGE_SHARD_SEC / (256 * LARGE_SHARD_SIZE))
#define SMALL_CLUSTERS (SMALL_SHARD_SEC / (256 * SMALL_SHARD_SIZE))
#define ARBITRAGE ((LARGE_CLUSTERS + SMALL_CLUSTERS) / 2)

#define SLAB_SIZE (sizeof(struct Slab) + (size_t)SMALL_CLUSTERS * 256 * SMALL_SHARD_SIZE + (size_t)LARGE_CLUSTERS * 256 * LARGE_SHARD_SIZE)

static struct Slab* slab;

struct Slab
{
    __m256i large[LARGE_CLUSTERS];
    __m256i small[SMALL_CLUSTERS];
    // This is going to be used to track the length/concurrency of shards.
    // Every 2 bits of shard presence metadata is represented by 1 bit of arbitrage data.
    __m256i arbitrage[ARBITRAGE];
    struct Slab* next;
};

struct Slab* slab_init()
{
    void* raw = mmap(NULL, SLAB_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED)
        return NULL;

    struct Slab* ptr = (struct Slab*)(((uintptr_t)raw + 31) & ~31);
    size_t bitmapBytes = (LARGE_CLUSTERS + SMALL_CLUSTERS) * sizeof(__m256i);
    memset(ptr, 255, bitmapBytes);
    memset((char*)ptr + bitmapBytes, 0, sizeof(struct Slab) - bitmapBytes);
    return ptr;
}

int shard_lookup(void* ptr)
{
    // TODO:
    return -1;
}

void* small_lookup(struct Slab* slab, int shard)
{
    return (char*)slab + sizeof(struct Slab) + ((size_t)shard * SMALL_SHARD_SIZE);
}

void* large_lookup(struct Slab* slab, int shard)
{
    return (char*)slab + sizeof(struct Slab) + ((size_t)SMALL_CLUSTERS * 256 * SMALL_SHARD_SIZE) + ((size_t)shard * LARGE_SHARD_SIZE);
}

void* mzalloc_small(size_t size)
{
    if (slab == NULL)
    {
        slab = slab_init();
        if (slab == NULL)
            return NULL;
    }

    int len = (size + SMALL_SHARD_SIZE - 1) / SMALL_SHARD_SIZE;
    struct Slab* cur = slab;
    while (1)
    {
        int shard = -1;
        for (int i = 0; i < SMALL_CLUSTERS; i++)
        {
            int res = bmp_recode(cur->small + i, len);
            if (res != -1)
            {
                shard = i * 256 + res;
                break;
            }
        }

        if (shard == -1)
        {
            if (cur->next == NULL)
            {
                cur->next = slab_init();
                if (cur->next == NULL)
                    return NULL;
            }

            cur = cur->next;
            continue;
        }

        return small_lookup(cur, shard);
    }
}

void* mzalloc_large(size_t size)
{
    if (slab == NULL)
    {
        slab = slab_init();
        if (slab == NULL)
            return NULL;
    }

    int len = (size + LARGE_SHARD_SIZE - 1) / LARGE_SHARD_SIZE;
    struct Slab* cur = slab;
    while (1)
    {
        int shard = -1;
        for (int i = 0; i < LARGE_CLUSTERS; i++)
        {
            int res = bmp_recode(cur->large + i, len);
            if (res != -1)
            {
                shard = i * 256 + res;
                break;
            }
        }

        if (shard == -1)
        {
            if (cur->next == NULL)
            {
                cur->next = slab_init();
                if (cur->next == NULL)
                    return NULL;
            }

            cur = cur->next;
            continue;
        }

        return large_lookup(cur, shard);
    }
}

void* mzalloc(size_t size)
{
    if (size >= 512)
        return mzalloc_large(size);
    else
        return mzalloc_small(size);
}

void free(void* ptr)
{
    // TODO:
}
