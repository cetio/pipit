#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
// TODO: Use SIMDE?
#include <immintrin.h>

#define BUCKET_SIZE 256

// struct _Bucket
// {
//     // Each bit corresponds to true or false for an entry's presence.
//     __m256i bitmap;
//     // Followed by BUCKET_SIZE entries and the next bucket, if exists.
// };

struct _Pair
{
    uint32_t key;
    void* value;
};

struct Map
{
    struct _Pair* head;
    struct _Pair* tail;
};

// TODO: Fuzzy key binary traversal over buckets?
// TODO: Bitmap presence at bucket start for bulk processing?
// TODO: Allocator support?
// TODO: Thread-safety
// TODO: Memory mapped file as a buffer

/// @brief Initializes a map with a zeroed head.
/// @param map The map to be initialized.
/// @return NULL if the map failed to initialize, otherwise truthy.
static inline int map_init(struct Map* map)
{
    map->head = calloc(BUCKET_SIZE, sizeof(struct _Pair));
    // The tail will always be the last bucket.
    map->tail = map->head;

    return map->head != NULL;
}

/// @brief For internal use only, or external use if you're feeling spicy. \
/// @brief Generates 4 32-bit round keys from `key`, within the range of 0..BUFFER_SIZE.
/// @param key The 32-bit seeding key.
/// @return Vector containing 4 32-bit round keys.
static inline __m128i _map_rotbatch(uint32_t key)
{
    // There's not really a methodology to this, just fast bit modification followed by
    // constraining to the number of entries available (0..BUFFER_SIZE.)
    __m128i keys = _mm_set1_epi32(key);
    keys = _mm_shuffle_epi8(keys, _mm_setr_epi8(
        0, 1, 2, 3, 15, 11, 5, 7, 13, 14, 10, 6, 4, 9, 8, 12));
    keys = _mm_mul_epu32(keys, _mm_setr_epi32(
        1, 452747683, 637270343, 892823233));
    return _mm_and_si128(keys, _mm_set1_epi32(BUCKET_SIZE - 1));
}

/// @brief For internal use only, or external use if you're feeling spicy. \
/// @brief Searches a map for a key-value pair with the given key and predicate.
/// @param key The 32-bit seeding key.
/// @param predicate Vector of 8 32-bit values to act as a search predicate.
/// @return Vector containing 4 32-bit round keys.
static inline struct _Pair* _map_search(
    const struct Map* map, 
    uint32_t key, 
    __m256i predicate)
{
    struct _Pair* cur = map->head;
    int len = ((map->tail - map->head) / BUCKET_SIZE);
    // Generate 4 32-bit round keys from the seeding key so we can mitigate hash collisions.
    // This effectively lets us reseed 8 times for every 2 buckets in constant time.
    __m128i keys = _map_rotbatch(key);

    while (len >= 0)
    {
        __m256i bmp;
        // Populate the first half of the vector with the first bucket we see.
        for (int i = 0; i < 4; i++)
            ((uint32_t*)&bmp)[i + 4] = cur[((uint32_t*)&keys)[i]].key;
        cur = len == 0 ? cur : cur + BUCKET_SIZE;

        // Populate the second half of the vector with either the same values or the next bucket.
        // By doing it this way instead of a conditional we are preventing a branch.
        for (int i = 0; i < 4; i++)
            ((uint32_t*)&bmp)[i] = cur[((uint32_t*)&keys)[i]].key;
        cur += BUCKET_SIZE;

        // This could be a single _mm256_cmpeq_epi32_mask operation but AVX512 is tricky and generally
        // most devices don't have it, so this suffices despite being a little slower.
        int mask = _mm256_movemask_epi8(_mm256_cmpeq_epi32(bmp, predicate));
        if (mask != 0)
        {
            int idx = __builtin_ctz(mask);
            struct _Pair* _cur = cur - BUCKET_SIZE;
            // Backtrack to bucket hit.
            _cur = idx <= 3 ? _cur : _cur - BUCKET_SIZE;
            return _cur + ((uint32_t*)&keys)[idx] % 4;
        }

        len -= 2;
    }
    
    return NULL;
}

/// @brief Retrieves the value in `map` at `key`.
/// @param map The map to operate on.
/// @param key The entry key to retrieve from.
/// @return The entry value or NULL if not present.
static inline void* map_get(const struct Map* map, uint32_t key)
{
    struct _Pair* pair = _map_search(map, key, _mm256_set1_epi32(key));
    if (pair == NULL)
        return NULL;

    return pair->value;
}

/// @brief Hard resets an entry in `map`, removing all entry state at `key`.
/// @param map The map to operate on.
/// @param key The entry key which will be reset.
/// @return The entry value or NULL if not present.
static inline void* map_reset(struct Map* map, uint32_t key)
{
    struct _Pair* pair = _map_search(map, key, _mm256_set1_epi32(key));
    if (pair == NULL)
        return NULL;

    void* ret = pair->value;
    pair->key = 0;
    pair->value = NULL;
    return ret;
}

/// @brief Creates an entry in `map` with `key` `value` pair.
/// @attention This will create a new entry no matter what, and may create an entry for a pre-existing key.
/// @attention I have no clue what the implications of this are, so don't give it an pre-existing key.
/// @param map The map to operate on.
/// @param key The entry key to be set for the pair.
/// @param value The entry value to be set for the pair.
static inline void map_set(struct Map* map, uint32_t key, void* value)
{
    //struct _Pair* pair = _map_search(map, key, _mm256_setzero_si256());
    struct _Pair* pair = NULL;
    if (pair == NULL)
    {
        size_t size = sizeof(struct _Pair) * ((map->tail - map->head) + BUCKET_SIZE);

        map->head = (struct _Pair*)realloc(map->head, size + (sizeof(struct _Pair) * BUCKET_SIZE));
        map->tail = ((void*)map->head) + size;
        memset(map->tail, 0, (sizeof(struct _Pair) * BUCKET_SIZE));

        map->tail[key % BUCKET_SIZE].key = key;
        map->tail[key % BUCKET_SIZE].value = value;
        return;
    }

    pair->key = key;
    pair->value = value;
}