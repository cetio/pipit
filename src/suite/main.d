import std.stdio;
import tern;
import inteli;

// alias F = void function(ref __m256i);
// F[] funcs = [
//     _mm256_xor_si256
// ];

int differential(__m256i a, __m256i b)
{
    a = _mm256_and_si256(a, b);
    a = _mm256_
}

int diffusion(F)(string str)
{
    foreach (i; 0..256)
    {
        foreach (j; 0..(256 - i))
        {
            auto x = F(str);
            str[j / 8] ^= 1 << (j % 8);
            auto y = F(str);
        }
    }
}

void main()
{
    //assert(0, "The test suite should not be built or run directly. Please run in unittest mode.");
}