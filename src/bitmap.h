#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>
#include <stdio.h>

// TODO: Refactor & documentation.
// TODO: Bulk processing & utility functions.
// TODO: I have a hunch AVX-512 lzcnt should be substantially faster.

static inline __m256i bmp_shr1(__m256i bmp)
{
    __m256i carry = _mm256_set1_epi16(0b0000000100000001);
    carry = _mm256_and_si256(carry, bmp);
    carry = _mm256_slli_epi16(carry, 7);
    __m128i hi = _mm256_extracti128_si256(carry, 1);
    hi = _mm_slli_si128(hi, 1);
    __m128i lo = _mm256_castsi256_si128(carry);
    hi = _mm_insert_epi8(hi, _mm_extract_epi8(lo, 15), 0);
    lo = _mm_slli_si128(lo, 1);
    carry = _mm256_set_m128i(hi, lo);
    // TODO: I have no idea how this works or if it does, frankly.
    __m256i shifted = _mm256_srli_epi16(bmp, 1);                  // shift each 16-bit lane
    __m256i mask    = _mm256_set1_epi16(0x7F7F);                  // 0x7F per byte
    bmp = _mm256_and_si256(shifted, mask);                        // keep only the 7 LSB of each byte
    bmp = _mm256_or_si256(bmp, carry);
    return bmp;
}

// TODO: I think it could possibly be more efficient to do something else?
static inline __m256i bmp_shl1(__m256i bmp)
{
    __m256i carry = _mm256_set1_epi16(0b1000000010000000);
    carry = _mm256_and_si256(carry, bmp);
    carry = _mm256_srli_epi16(carry, 7);
    __m128i lo = _mm256_castsi256_si128(carry);
    lo = _mm_srli_si128(lo, 1);
    __m128i hi = _mm256_extracti128_si256(carry, 1);
    lo = _mm_insert_epi8(lo, _mm_extract_epi8(hi, 0), 15);
    hi = _mm_srli_si128(hi, 1);
    carry = _mm256_set_m128i(hi, lo);
    // Shift each bit by 1 left per byte and then OR the 2 for the carry.
    bmp = _mm256_add_epi8(bmp, bmp);
    bmp = _mm256_or_si256(bmp, carry);
    return bmp;
}

/// @brief Condenses a 256-bit bitmap to reduce to len number successive bit patterns.
/// In effect, this means that the only bits set were preceded by len number bits.
/// @param bmp The 256-bit bitmap to be condensed.
/// @param len The length of the bit pattern.
/// @return The provided 256-bit bitmap bmp condensed to len bit patterns.
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

/// @brief Expands out 256-bit bitmap based on a given bit position and length.
/// @param pos The initial bit position.
/// @param len The length of the bit pattern.
/// @return The newly generated 256-bit bitmap with len bits at pos.
static inline __m256i bmp_expand(int pos, int len)
{
    __m256i bmp = _mm256_set_epi8(
        1, 0, 0, 0, 0, 0, 0,
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

    *ptr = _mm256_andnot_si256(bmp_expand(mask, len), *ptr);
    return mask;
}

static inline void bmp_set(__m256i* ptr, int pos, int len)
{
    *ptr = _mm256_or_si256(*ptr, bmp_expand(pos, len));
}
