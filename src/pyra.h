#include <stdint.h>
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

void pyra_sb128(struct PYRA* s);

void pyra_sb256(struct PYRA* s);

void pyra_invsb128(struct PYRA* s);

void pyra_invsb256(struct PYRA* s);

void pyra_init(struct PYRA* s, uint64_t seed, char* key);

int pyra_encrypt(struct PYRA* s, uint8_t* data, size_t len);

void __pyra_test_init();