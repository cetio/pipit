// Pyra-2 is a block cipher intended to be an incredibly fast, SIMD-leveraging
// encryption algorithm for situations where you want near-realtime
// access to data on the fly. It is not perfect, and I have no professional
// background in encryption, but it is fairly secure from what I can tell,
// and yields high entropy results with a variety of test inputs.
//
// Initially Pyra-1 was a feistel cipher, but this is no longer the case,
// it is inspired by the pharoahs card shuffle and mixes and blends data
// repeatedly to yield a result that has no discernable pattern even if you were to
// generally have an idea of what the key is or what operations are being performed.
//
// This would likely benefit from some kind of side-channeling prevention, like
// including some random operations as a wrapper of some sort or during intermission of loops,
// since there is a lot of data that could be harvested during both encryption
// and decryption phases of Pyra-2.

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "immintrin.h"

#define PYRA_SUCCESS 0;
#define PYRA_UNALIGNED_LEN 1;
#define PYRA_UNALIGNED_DATA 2;

struct PYRA
{
    uint64_t seed;
    __m256i keys[4];
    __m128i phi;
    __m256i tau;
};

void pyra_sb128(struct PYRA* s)
{
    __m128i mask;

    for (int i = 0; i < 8; i++)
    {
        ((uint8_t*)&mask)[i] = -(s->seed & 1);
        ((uint8_t*)&mask)[15 - i] = -(s->seed & 1);

        s->seed ^= s->seed << 13;
        s->seed ^= s->seed >> 7;
        s->seed ^= s->seed << 17;
    }

    __m128i lo = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m128i hi = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    s->phi = _mm_or_si128(_mm_and_si128(mask, lo), _mm_andnot_si128(mask, hi));
}

void pyra_sb256(struct PYRA* s)
{
    __m256i mask;

    // Generate first lane.
    for (int i = 0; i < 8; i++)
    {
        ((uint8_t*)&mask)[i] = -(s->seed & 1);
        ((uint8_t*)&mask)[15 - i] = -(s->seed & 1);

        s->seed ^= s->seed << 13;
        s->seed ^= s->seed >> 7;
        s->seed ^= s->seed << 17;
    }

    // Generate second lane.
    for (int i = 15; i < 32; i++)
    {
        ((uint8_t*)&mask)[i] = -(s->seed & 1);
        ((uint8_t*)&mask)[31 - i] = -(s->seed & 1);

        s->seed ^= s->seed << 13;
        s->seed ^= s->seed >> 7;
        s->seed ^= s->seed << 17;
    }

    __m256i lo = _mm256_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m256i hi = _mm256_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    s->tau = _mm256_or_si256(_mm256_and_si256(mask, lo), _mm256_andnot_si256(mask, hi));
}

void pyra_invsb128(struct PYRA* s)
{
    __m128i mask;

    for (int i = 0; i < 8; i++)
    {
        ((uint8_t*)&mask)[i] = -(s->seed & 1);
        ((uint8_t*)&mask)[15 - i] = -(s->seed & 1);

        s->seed ^= s->seed << 13;
        s->seed ^= s->seed >> 7;
        s->seed ^= s->seed << 17;
    }

    __m128i lo = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m128i hi = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    s->phi = _mm_or_si128(_mm_and_si128(mask, hi), _mm_andnot_si128(mask, lo));
}

void pyra_invsb256(struct PYRA* s)
{
    __m256i mask;

    for (int i = 0; i < 8; i++)
    {
        ((uint8_t*)&mask)[i] = -(s->seed & 1);
        ((uint8_t*)&mask)[15 - i] = -(s->seed & 1);

        s->seed ^= s->seed << 13;
        s->seed ^= s->seed >> 7;
        s->seed ^= s->seed << 17;
    }

    for (int i = 15; i < 32; i++)
    {
        ((uint8_t*)&mask)[i] = -(s->seed & 1);
        ((uint8_t*)&mask)[31 - i] = -(s->seed & 1);

        s->seed ^= s->seed << 13;
        s->seed ^= s->seed >> 7;
        s->seed ^= s->seed << 17;
    }

    __m256i lo = _mm256_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m256i hi = _mm256_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    s->tau = _mm256_or_si256(_mm256_and_si256(mask, hi), _mm256_andnot_si256(mask, lo));
}

void pyra_init(struct PYRA* s, uint64_t seed, char* key)
{
    s->seed = seed | 1;
    s->keys[0] = _mm256_loadu_si256((__m256i*)key);
    s->keys[1] = s->keys[0];
    s->keys[2] = s->keys[0];
    s->keys[3] = s->keys[0];

    pyra_sb256(s);

    for (int i = 0; i < 4; i++)
    {
        __m256i tmp = _mm256_permute4x64_epi64(s->keys[i], 0b00100111);
        tmp = _mm256_shuffle_epi8(tmp, s->tau);

        s->keys[i] = _mm256_mul_epu32(s->keys[i], tmp);
    }

    // // Flip the halves of the final 2 keys.
    // s->keys[2] = _mm256_permute2x128_si256(s->keys[2], s->keys[2], 1);
    // s->keys[3] = _mm256_permute2x128_si256(s->keys[3], s->keys[3], 1);

    // // Finalize the keys by xoring the first 2 by the last 2 keys.
    // s->keys[0] = _mm256_xor_si256(s->keys[0], s->keys[3]);
    // s->keys[1] = _mm256_xor_si256(s->keys[1], s->keys[2]);p
}

void __pyra_test_init()
{
    struct PYRA* s = (struct PYRA*)aligned_alloc(32, sizeof(struct PYRA));
    struct PYRA* p = (struct PYRA*)aligned_alloc(32, sizeof(struct PYRA));
    char* key = malloc(32);
    strcpy(key, "25845CE533E8253D1965F6E65411F");

    pyra_init(s, 0, key);
    *p = *s;

    int diff;
    for (int i = 0; i < 256; i++)
    {
        key[i / 8] ^= 1 << (i % 8);
        pyra_init(s, 0, key);

        for (int j = 0; j < 256; j++)
        {
            if ((s->tau[j / 8] & (1 << (j % 8))) == (p->tau[j / 8] & (1 << (j % 8))))
                diff++;
        }

        for (int j = 0; j < 32; j++)
            printf("%d ", ((uint8_t*)&s->tau)[j]);

        printf("\n --");

        for (int j = 0; j < 32; j++)
            printf("%d ", ((uint8_t*)&p->tau)[j]);

        printf("\n");

        *p = *s;
    }

    printf("%d\n", diff / 256);
}

// printf("\nKEY 1: ");
// for (int i = 0; i < 4; i++)
//     printf("%d, ", s->keys[0][i]);

// printf("\nKEY 2: ");
// for (int i = 0; i < 4; i++)
//     printf("%d, ", s->keys[1][i]);

// printf("\nKEY 3: ");
// for (int i = 0; i < 4; i++)
//     printf("%d, ", s->keys[2][i]);

// printf("\nKEY 4: ");
// for (int i = 0; i < 4; i++)
//     printf("%d, ", s->keys[3][i]);

int pyra_encrypt(struct PYRA* s, uint8_t* data, size_t len)
{
    //if ((len == 0) || ((len & (len - 1)) != 0))
    if (len % 32 != 0)
        return PYRA_UNALIGNED_LEN;

    if (data == NULL || ((size_t)data % 32) != 0)
        return PYRA_UNALIGNED_DATA;

    for (int i = 0; i < (len / 32); i++)
    {
        __m256i v = _mm256_loadu_si256((__m256i*)data + i);

        v = _mm256_sub_epi64(v, s->keys[2]);
        v = _mm256_xor_si256(v, s->keys[1]);

        for (int i = 0; i < s->seed % 8; i++)
            v = _mm256_shuffle_epi8(v, s->tau);

        v = _mm256_add_epi64(v, s->keys[0]);
        v = _mm256_xor_si256(v, s->keys[3]);

        for (int i = 0; i < s->seed % 8; i++)
            v = _mm256_shuffle_epi8(v, s->tau);

        _mm256_storeu_si256((__m256i*)data + i, v);
    }

    return PYRA_SUCCESS;
}

/*int decrypt(uint8_t* data, size_t* len, char* key)
{
    if (data == NULL || len == NULL)
        return 0;

    const struct s[4] = {
        derive(0x0c0b6479, key),
        derive(0x8ea853bc, key),
        derive(0x79b953f7, key),
        derive(0xfe778533, key)
    };

    const uint64_t SLOTS[16] = {
        s[0][0], s[0][1], s[0][2], s[0][3],
        s[1][0], s[1][1], s[1][2], s[1][3],
        s[2][0], s[2][1], s[2][2], s[2][3],
        s[3][0], s[3][1], s[3][2], s[3][3]
    };

    uint64_t seed = SLOTS[0];

    __m128i mm16 = invsb128(seed);
    __m256i mm32 = invsb256(seed);

    for (int i = (*len / 32); i < (*len / 16); i++)
        seed ^= SLOTS[seed % 16];

    for (int i = 0; i < (*len / 32); i++)
    {
        __m256i v = _mm256_load_si256((__m256i*)data + i);

        for (int i = 0; i < seed % 8; i++)
            v = _mm256_shuffle_epi8(v, mm32);

        v = _mm256_xor_si256(v, s[3]);
        v = _mm256_sub_epi64(v, s[0]);
        v = _mm256_sub_epi64(v, _mm256_set1_epi64x(seed));

        for (int i = 0; i < seed % 8; i++)
            v = _mm256_shuffle_epi8(v, mm32);

        v = _mm256_xor_si256(v, s[1]);
        v = _mm256_add_epi64(v, s[2]);

        _mm256_store_si256((__m256i*)data + i, v);
    }

    seed = SLOTS[0];

    for (int i = (*len / 32); i < (*len / 16); i++)
    {
        int j = i - (*len / 32);

        __m128i hi = _mm_load_si128((__m128i*)data + i);
        __m128i lo = _mm_load_si128((__m128i*)data + j);
        __m128i pm16 = mix128a(seed);

        __m128i tmp = _mm_blendv_epi8(hi, lo, pm16);
        lo = _mm_blendv_epi8(lo, hi, pm16);
        hi = tmp;

        for (int i = 0; i < seed % 8; i++)
        {
            lo = _mm_shuffle_epi8(lo, mm16);
            hi = _mm_shuffle_epi8(hi, mm16);
        }

        _mm_store_si128((__m128i*)data + j, lo);
        _mm_store_si128((__m128i*)data + i, hi);

        seed ^= SLOTS[seed % 16];
    }

    return 1;
}*/