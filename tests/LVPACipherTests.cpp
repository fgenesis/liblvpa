#include "LVPAInternal.h"
#include <cstdio>

#include "LVPACipherTests.h"
#include "LVPAStreamCipher.h"

#ifdef LVPA_NAMESPACE
  using namespace LVPA_NAMESPACE;
#endif

typedef union { void *vp; size_t s; uint32 u32; unsigned long l; } my_align_t; // idea from lzo

uint8 k0[] = { 0xef, 0x01, 0x23, 0x45 };
uint8 v0[] = { 0,0,0,0,0,0,0,0,0,0,0,0 }; // 12 zeros
uint8 r0_RC4Cipher[]       = { 0xd6, 0xa1, 0x41, 0xa7, 0xec, 0x3c, 0x38, 0xdf, 0xbd, 0x61, 0x5a, 0x11 }; // <- original rc4 result
uint8 r0_HPRC4LikeCipher[] = { 0xd6, 0xd4, 0x61, 0x81, 0xa1, 0x36, 0x7b, 0x29, 0x41, 0x25, 0x3b, 0x1d }; // <- noted for consistency

#define CPALLOC(to, from) \
    uint8 to[sizeof(from)]; \
    memcpy(to, from, sizeof(from));

#define STATIC_TEST_CIPHER(algo, key, data0, data1) \
{ \
    CPALLOC(_k, key); CPALLOC(_d0, data0); CPALLOC(_d1, data1 ## _ ## algo); \
    return TestCipherHelper<algo>(_k, sizeof(_k), _d0, _d1, sizeof(_d0), 0, false); \
}

#define STATIC_TEST_CIPHER_BYTEWISE(algo, key, data0, data1) \
{ \
    CPALLOC(_k, key); CPALLOC(_d0, data0); CPALLOC(_d1, data1 ## _ ## algo); \
    return TestCipherHelper<algo>(_k, sizeof(_k), _d0, _d1, sizeof(_d0), 0, true); \
}

#define STATIC_TEST_CIPHER_WARM_2(algo, key, data0, warmup) \
{ \
    CPALLOC(_k, key); CPALLOC(_d0, data0); \
    return TestCipherHelper<algo>(_k, sizeof(_k), _d0, NULL, sizeof(_d0), warmup, false); \
}

#define STATIC_TEST_CIPHER_WARM(algo, key, data0) \
{ \
    STATIC_TEST_CIPHER_WARM_2(algo, key, data0, 32); \
    STATIC_TEST_CIPHER_WARM_2(algo, key, data0, 1); \
    STATIC_TEST_CIPHER_WARM_2(algo, key, data0, 10); \
    STATIC_TEST_CIPHER_WARM_2(algo, key, data0, 43); \
    STATIC_TEST_CIPHER_WARM_2(algo, key, data0, 128); \
    STATIC_TEST_CIPHER_WARM_2(algo, key, data0, 171); \
    STATIC_TEST_CIPHER_WARM_2(algo, key, data0, 600); \
}

static void printchex(const char *in, unsigned int len, bool spaces = true)
{
    unsigned int i;
    putchar('[');
    if(spaces)
        for(i = 0; i < len; i++)
            printf("%x ", in[i]);
    else
        for(i = 0; i < len; i++)
            printf("%x", in[i]);
    puts("]\n");
}

template <typename T> static int TestCipherHelper(uint8 *k, uint32 ks, uint8 *v, uint8 *r, uint32 s, uint32 warmup, bool bytewise)
{
    std::vector<uint8> o(s);
    memcpy(&o[0], v, s);
    {
        T rc;
        rc.Init(k, ks);
        rc.WarmUp(warmup);
        if(bytewise)
            for(uint32 i = 0; i < s; ++i)
                rc.Apply(v + i, 1);
        else
            rc.Apply(v, s);
    }
    if(r && memcmp(v, r, s))
    {
        logerror("Expected memory:");
        printchex((char*)r, s, true);
        logerror("Actual memory:");
        printchex((char*)v, s, true);
        return 1;
    }
    {
        T rc;
        rc.Init(k, ks);
        rc.WarmUp(warmup);
        if(bytewise)
            for(uint32 i = 0; i < s; ++i)
                rc.Apply(v + i, 1);
        else
            rc.Apply(v, s);
    }
    if(memcmp(v, &o[0], s))
    {
        logerror("Expected memory:");
        printchex((char*)&o[0], s, true);
        logerror("Actual memory:");
        printchex((char*)v, s, true);
        return 2;
    }
    return 0;
}

template <typename T> static int TestCipherMassive()
{
    my_align_t smem[ (80000 + (sizeof(my_align_t) - 1)) / sizeof(my_align_t) ];
    uint8 *mem = (uint8*)&smem;
    uint8 key[] =
    {
        0x33, 0x60, 0x12, 0x58,
        0x01, 0x04, 0x59, 0x21,
        0x69, 0x00, 0xe2, 0xff,
        0xa6, 0x2e, 0xba, 0x7c,
        0xe4, 0x40, 0x55, 0xa6,
        0x19, 0xd9, 0xdd, 0x7f,
        0xc3, 0x16, 0x6f, 0x94,
        0x87, 0xf7, 0xcb, 0x27,
        0x29, 0x12, 0x42, 0x64
    };
    for(uint32 i = 0; i < 80000; ++i)
        mem[i] = uint8(i);

    {
        T rc;
        rc.Init(key, sizeof(key));
        for(uint32 i = 0; i < 400; ++i)
            rc.Apply(mem, 80000);
    }

    {
        T rc;
        rc.Init(key, sizeof(key));
        for(uint32 i = 0; i < 400; ++i)
            rc.Apply(mem, 80000);
    }

    for(uint32 i = 0; i < 80000; ++i)
        if(mem[i] != uint8(i))
            return 1;
    return 0;
}

int TestRC4()
{
    STATIC_TEST_CIPHER(RC4Cipher, k0, v0, r0);
}

int TestHPRC4Like()
{
    {
        // first, be sure it will not overwrite memory it should not due to uint32 blocks
        uint8 mem[8];
        memset(mem, 0, 8);
        uint8 key[4] = { 0x45, 0x7a, 0x09, 0xf5 };
        HPRC4LikeCipher c;
        c.Init(key, 4);
        // do it very often to be sure its not bad luck
        for(uint32 i = 0; i < 256; ++i)
        {
            c.Apply(mem, 7);
            if(mem[7])
                return 9;
        }
    }
    STATIC_TEST_CIPHER(HPRC4LikeCipher, k0, v0, r0);
}

int TestHPRC4LikeBytewise()
{
    STATIC_TEST_CIPHER_BYTEWISE(HPRC4LikeCipher, k0, v0, r0);
}

int TestRC4Warm()
{
    STATIC_TEST_CIPHER_WARM(RC4Cipher, k0, v0);
}

int TestHPRC4LikeWarm()
{
    STATIC_TEST_CIPHER_WARM(HPRC4LikeCipher, k0, v0);
}

int TestRC4Massive()
{
    return TestCipherMassive<RC4Cipher>();
}

int TestHPRC4LikeMassive()
{
    return TestCipherMassive<HPRC4LikeCipher>();
}
