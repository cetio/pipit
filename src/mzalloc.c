#include <immintrin.h>
#include <stdint.h>
#include <sys/mman.h>
#include "bitmap.h"
#include <stdio.h>
#include <string.h>

#define SLAB_SIZE 1024 * 1024 * 4
#define LARGE_SHARD_SIZE 256
#define LARGE_SHARD_SEC 1024 * 1024
#define SMALL_SHARD_SIZE 48
#define SMALL_SHARD_SEC 1024 * 1024 * 3
#define LARGE_CLUSTERS LARGE_SHARD_SEC / (256 * LARGE_SHARD_SIZE)
#define SMALL_CLUSTERS SMALL_SHARD_SEC / (256 * SMALL_SHARD_SIZE)
#define ARBITRAGE (LARGE_CLUSTERS + SMALL_CLUSTERS) / 2
#define METADATA ((LARGE_CLUSTERS + SMALL_CLUSTERS) * 32) + ARBITRAGE

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
    void* ptr = mmap(NULL, SLAB_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ptr += 32 - ((size_t)ptr % 32);
    memset(ptr, 255, METADATA);
    memset(ptr + METADATA, 0, sizeof(struct Slab) - METADATA);
    return (struct Slab*)ptr;
}

int shard_lookup(void* ptr)
{
    // TODO:
}

void* small_lookup(struct Slab* slab, int shard)
{
    return (void*)slab + sizeof(struct Slab) + (shard * SMALL_SHARD_SIZE);
}

void* large_lookup(struct Slab* slab, int shard)
{
    return (void*)slab + sizeof(struct Slab) + (SMALL_CLUSTERS * SMALL_SHARD_SIZE) + (shard * LARGE_SHARD_SIZE);
}

void arbitrage_set(struct Slab* slab, int shard, int len)
{
    if (len < 2)
        return;

    void* ptr = (void*)slab + sizeof(struct Slab) + (SMALL_CLUSTERS * SMALL_SHARD_SIZE) +
        (LARGE_CLUSTERS * LARGE_SHARD_SIZE) + (256 * ((shard / 2) / 256));
    bmp_set(ptr, (shard / 2) + 1, len / 2);
    111110111

}

void* mzalloc_small(size_t size)
{
    if (slab == NULL)
        slab = slab_init();

    struct Slab* cur = slab;
    int len = (size + (LARGE_SHARD_SIZE - (size % LARGE_SHARD_SIZE))) / LARGE_SHARD_SIZE;
    while (1)
    {
        int shard = -1;
        for (int i = 0; i < SMALL_CLUSTERS; i++)
        {
            int res = bmp_recode(cur->small + i, len);
            shard = res == -1 ? shard : res;
        }

        if (shard == -1)
        {
            // TODO: Improve processing here?
            if (cur->next == NULL)
            {
                //printf("new slab\n");
                cur->next = slab_init();
            }

            cur = cur->next;
            continue;
        }

        //arbitrage_set(cur, shard, len);
        return small_lookup(cur, shard);
    }

    return NULL;
}

void* mzalloc_large(size_t size)
{
    if (slab == NULL)
        slab = slab_init();

    struct Slab* cur = slab;
    int len = (size + (LARGE_SHARD_SIZE - (size % LARGE_SHARD_SIZE))) / LARGE_SHARD_SIZE;
    while (1)
    {
        int shard = -1;
        for (int i = 0; i < LARGE_CLUSTERS; i++)
        {
            int res = bmp_recode(cur->large + i, len);
            shard = res == -1 ? shard : res;
        }

        if (shard == -1)
        {
            // TODO: Improve processing here?
            if (cur->next == NULL)
                cur->next = slab_init();

            cur = cur->next;
            continue;
        }

        //arbitrage_set(cur, shard, len);
        return large_lookup(cur, shard);
    }

    return NULL;
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
