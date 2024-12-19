#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
// TODO: Use SIMDE?
#include <immintrin.h>

// This would normally be 256 but I discovered sequences collide less with 255.
#define BUCKET_SIZE 256

struct Bucket
{
    // Each bit corresponds to true or false for an entry's presence.
    __m256i bitmap;
    // Followed by BUCKET_SIZE entries and the next bucket, if exists.
};

struct KV
{
    uint32_t key;
    void* value;
};

struct Map
{
    // We could make this grow exponentially but given the use case I need this for,
    // it shouldn't be a big deal and we can pretend everything is fine and dandy.
    struct KV* head;
    struct KV* tail;
};

// TODO: Fuzzy key binary traversal over buckets?
// TODO: Bitmap presence at bucket start for bulk processing?
// TODO: Allocator support?
// TODO: Thread-safety

static inline int map_init(struct Map* map)
{
    map->head = calloc(BUCKET_SIZE, sizeof(struct KV));
    // The tail will always be the last bucket.
    map->tail = map->head;

    return map->head != NULL;
}

/// @brief Retrieves the value in `map` at `key`.
/// @param map The map to operate on.
/// @param key The entry key to retrieve from.
/// @return The entry value or NULL if not present.
static inline void* map_get(const struct Map* map, uint32_t key)
{
    struct KV* cur = map->head;
    int len = ((map->tail - map->head) / BUCKET_SIZE) + 1;

    while (len > 0)
    {
        __m256i bmp;
        for (int i = 0; i < 4; i++)
        {
            ((uint32_t*)&bmp)[i] = cur[key % BUCKET_SIZE].key;
            
            if (--len <= 0)
                break;
            else
                cur += BUCKET_SIZE;
        }

        // This could be a single _mm256_cmpeq_epi32_mask operation but AVX512 is tricky and generally
        // most devices don't have it, so this suffices despite being a little slower.
        int mask = _mm256_movemask_epi8(_mm256_cmpeq_epi32(bmp, _mm256_set1_epi32(key)));
        if (mask)
        {
            // Backtrack to when the hit happened.
            cur -= __builtin_ctz(mask) * BUCKET_SIZE;
            return cur[key % BUCKET_SIZE].value;
        }
    }

    return NULL;
}

/// @brief Hard resets an entry in `map`, removing all entry state at `key`.
/// @param map The map to operate on.
/// @param key The entry key which will be reset.
/// @return The entry value or NULL if not present.
static inline void* map_reset(struct Map* map, uint32_t key)
{
    struct KV* cur = map->head;
    int len = ((map->tail - map->head) / BUCKET_SIZE) + 1;

    while (len > 0)
    {
        __m256i bmp;
        for (int i = 0; i < 4; i++)
        {
            ((uint32_t*)&bmp)[i] = cur[key % BUCKET_SIZE].key;
            
            if (--len <= 0)
                break;
            else
                cur += BUCKET_SIZE;
        }

        // This could be a single _mm256_cmpeq_epi32_mask operation but AVX512 is tricky and generally
        // most devices don't have it, so this suffices despite being a little slower.
        int mask = _mm256_movemask_epi8(_mm256_cmpeq_epi32(bmp, _mm256_set1_epi32(key)));
        if (mask)
        {
            // Backtrack to when the hit happened.
            cur -= __builtin_ctz(mask) * BUCKET_SIZE;

            void* val = cur[key % BUCKET_SIZE].value;

            cur[key % BUCKET_SIZE].key = 0;
            cur[key % BUCKET_SIZE].value = NULL;

            return val;
        }
    }

    return NULL;
}

/// @brief Sets and or creates an entry in `map` with `key` `value` pair.
/// @param map The map to operate on.
/// @param key The entry key to set value of, may not be present.
/// @param value The entry value to be set for key.
static inline void map_set(struct Map* map, uint32_t key, void* value)
{
    struct KV* cur = map->head;
    int len = ((map->tail - map->head) / BUCKET_SIZE) + 1;
    //fprintf(stderr, "start %d %d\n", len, key);

    // __m128i keys = _mm_set1_epi32(key);
    // keys = _mm_mul_epu32(keys, _mm_set_epi32(465412117, 637270343, 452747683, 268116967));
    // keys = _mm_shuffle_epi8(keys, _mm_set_epi8(13, 0, 8, 4, 15, 11, 5, 7, 1, 14, 10, 6, 3, 9, 2, 12));
    // keys = _mm_mul_epu32(keys, _mm_set_epi32(637270343, 465412117, 268116967, 452747683));
    // keys = _mm_shuffle_epi8(keys, _mm_set_epi8(4, 8, 15, 13, 12, 5, 3, 7, 10, 6, 1, 14, 9, 11, 2, 0));
    // keys = _mm_and_si128(keys, _mm_set1_epi32(255));

    while (len > 0)
    {
        __m256i bmp = _mm256_set1_epi64x(-1);
        for (int i = 0; i < 8; i++)
        {
            // Could probably make this 8-bit result of checking if the key is zero,
            // might not help at all though, could be bad.
            ((uint32_t*)&bmp)[i] = cur[key % BUCKET_SIZE].key;
            
            if (--len <= 0)
                break;
            else
                cur += BUCKET_SIZE;
        }

        // This could be a single _mm256_cmpeq_epi32_mask operation but AVX512 is tricky and generally
        // most devices don't have it, so this suffices despite being a little slower.
        int mask = _mm256_movemask_epi8(_mm256_cmpeq_epi32(bmp, _mm256_setzero_si256()));
        //fprintf(stderr, "keys %d %d %d %d\n", ((int*)&keys)[0], ((int*)&keys)[1], ((int*)&keys)[2], ((int*)&keys)[3]);
        if (mask != 0)
        {
            // Backtrack to when the hit happened.
            cur -= __builtin_ctz(mask) * BUCKET_SIZE;
            //fprintf(stderr, "clz %d %d\n", __builtin_ctz(mask), bmp[1]);

            cur[key % BUCKET_SIZE].key = key;
            //fprintf(stderr, "set %d\n", key % BUCKET_SIZE);
            cur[key % BUCKET_SIZE].value = value;

            return;
        }
    }

    size_t size = sizeof(struct KV) * (map->tail - map->head) + BUCKET_SIZE;

    // Is this bad? realloc may not do what we want but that might be fine because you've got bigger issues...
    map->head = (struct KV*)realloc(map->head, size + BUCKET_SIZE);
    map->tail = map->head + size;

    map->tail[key % BUCKET_SIZE].key = key;
    map->tail[key % BUCKET_SIZE].value = value;
}