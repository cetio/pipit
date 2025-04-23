#include <immintrin.h>
#include <stdio.h>

// TODO: Refactor & documentation.
// TODO: Bulk processing & utility functions.
// TODO: I have a hunch AVX-512 lzcnt should be substantially faster.
// TODO: TEST!

// TODO: I don't think this works.
static inline __m256i bmp_shr1(__m256i bmp)
{
    __m256i tmp = bmp;
    tmp = _mm256_and_si256(tmp, _mm256_set1_epi16(0b0000000000000001));
    tmp = _mm256_loadu_si256((__m256i*)((char*)&tmp - 1));
    tmp = _mm256_insert_epi8(tmp, 0, 0);
    tmp = _mm256_slli_epi16(tmp, 15);
    bmp = _mm256_or_si256(_mm256_srli_epi16(bmp, 1), tmp);
    return bmp;
}

static inline __m256i bmp_shl1(__m256i bmp)
{
    __m256i tmp = bmp;
    tmp = _mm256_and_si256(tmp, _mm256_set1_epi16(0b1000000000000000));
    tmp = _mm256_loadu_si256((__m256i*)((char*)&tmp + 1));
    tmp = _mm256_insert_epi8(tmp, 15, 0);
    tmp = _mm256_srli_epi16(tmp, 15);
    bmp = _mm256_or_si256(_mm256_slli_epi16(bmp, 1), tmp);
    return bmp;
}

static inline __m256i bmp_condense(__m256i bmp, int len)
{
    // This is really quite simple but ends up being very convoluted,
    // in theory this is:
    //
    // for (int i = 0; i < len; i++)
    //     x &= x << 1;
    //
    // Where bits are condensed bitwise into pairs of num.
    while (--len > 0)
        bmp = _mm256_and_si256(bmp, bmp_shl1(bmp));
    return bmp;
}

static inline __m256i bmp_expand(int pos, int len)
{
    __m256i bmp = _mm256_set_epi8(
        0b10000000, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0
    );

    while (--len > 0)
        bmp = _mm256_or_si256(bmp, bmp_shr1(bmp));

    while (--pos > 0)
        bmp = bmp_shr1(bmp);
    return bmp;
}

/// @brief Searches for the first successive set bit pattern of len in the given bitmap.
/// @param bmp The 256-bit bitmap to be searched.
/// @param len The length of successive bits to match for.
/// @return The index of the first set bit in the pattern, or -1 if not found.
static inline int bmp_decode(__m256i bmp, int len)
{
    bmp = bmp_condense(bmp, len);

    int mask = _mm256_cmpneq_epi8_mask(bmp, _mm256_setzero_si256());
    if (mask == 0)
        return -1;
    else if (mask & 0b11110000)
        bmp = _mm256_permute4x64_epi64(bmp, 0b01001110);

    mask = __builtin_ctz(mask);
    bmp = _mm256_shuffle_epi8(bmp, _mm256_set1_epi8(15 - (mask % 16)));
    return __builtin_ctz(_mm256_extract_epi8(bmp, 0)) + (mask << 3);
}

/// @brief Searches for the first successive set bit pattern of len in the given bitmap.
/// @param bmp The 256-bit bitmap to be searched.
/// @param len The length of successive bits to match for.
/// @return The index of the first set bit in the pattern, or -1 if not found.
static inline int bmp_recode(__m256i* ptr, int len)
{
    __m256i bmp = *ptr;
    bmp = bmp_condense(bmp, len);

    int mask = _mm256_cmpneq_epi8_mask(bmp, _mm256_setzero_si256());
    if (mask == 0)
        return -1;
    else if (mask & 0b11110000)
        bmp = _mm256_permute4x64_epi64(bmp, 0b01001110);

    mask = __builtin_ctz(mask);
    bmp = _mm256_shuffle_epi8(bmp, _mm256_set1_epi8(15 - (mask % 16)));
    // This is the index of the first set bit in the pattern.
    mask = __builtin_ctz(_mm256_extract_epi8(bmp, 0)) + (mask << 3);

    *ptr = _mm256_andnot_si256(*ptr, bmp_expand(mask, len));
    printf("%Ix\n", bmp_expand(mask, len)[0]);
    printf("%Ix\n", (*ptr)[0]);
    // printf("VECTOR\n\n");
    // bmp = *ptr;
    // for (int i = 0; i < 256; i++)
    // {
    //     printf("%s", ((bmp = bmp_shr1(bmp))[0] & 0b10000000) == 1 ? "1" : "0");
    // }
    return mask;
}

static inline void bmp_set(__m256i* ptr, int pos, int len)
{
    *ptr = _mm256_or_si256(*ptr, bmp_expand(pos, len));
}
