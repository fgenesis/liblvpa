#include "LVPAInternal.h"
#include "ByteConverter.h"
#include "LVPAStreamCipher.h"
#include "MersenneTwister.h"

LVPA_NAMESPACE_START

template <typename T> inline static void iswap(T& a, T& b)
{
    /*
    a ^= b;
    b ^= a;
    a ^= b;
    */

    // tested to be ~30% faster than the code above for T=uint8, and roughly equal for T=uint32
    T c = a;
    a = b;
    b = c;
}

void ISymmetricCipher::WarmUp(uint32 size)
{
    volatile uint8 wrkmem[128]; // does not have to be initialized, we will never read from it
    uint32 i = 0;
    for( ; i < size; i += 128)
        Apply(wrkmem, 128);
    i -= size; // remaining
    Apply(wrkmem, i);
}

RC4Cipher::RC4Cipher()
: _x(0), _y(0)
{
    for(uint32 i = 0; i < 256; ++i)
        _sbox[i] = uint8(i);
}

void RC4Cipher::Init(const uint8 *key, uint32 size)
{
    uint8 a = 0, b = 0;
    for(uint32 i = 0; i < 256; ++i)
    {
        b += key[a] + _sbox[i];
        iswap(_sbox[i], _sbox[b]);
        ++a;
        a %= size;
    }
}

void RC4Cipher::Apply(uint8 *buf, uint32 size)
{
    uint8 x = _x, y = _y;
    uint8 *sbox = &_sbox[0];
    uint8 t;
    for(uint32 i = 0; i < size; ++i)
    {
        ++x;
        y += sbox[x];
        iswap(sbox[x], sbox[y]);
        t = sbox[x] + sbox[y];
        buf[i] ^= sbox[t];
    }
    _x = x;
    _y = y;
}


HPRC4LikeCipher::HPRC4LikeCipher()
: _x(0), _y(0), _rb(0)
{
}

void HPRC4LikeCipher::Init(const uint8 *key, uint32 size)
{
    // align key to 32 bits
    std::vector<uint32> keycopy((size + sizeof(uint32) - 1) / sizeof(uint32));
    DEBUG(ASSERT(keycopy.size() * sizeof(uint32) >= size));
    // the last 3 bytes may be incomplete, null those before copying
    keycopy[keycopy.size() - 1] = 0;
    memcpy(&keycopy[0], key, size);

#if IS_BIG_ENDIAN
    for(uint32 i = 0; i < keycopy.size(); ++i)
        ByteConverter::ToLittleEndian(keycopy[i]);
#endif

    MTRand mt;
    mt.seed((uint32*)&keycopy[0], keycopy.size());

    for(uint32 i = 0; i < 256; ++i)
        _sbox[i] = i | (mt.randInt() << 8); // lowest bit is always the exchange index, like in original RC4

    uint32 a = 0, b = 0;

    for(uint32 i = 0; i < std::max<uint32>(256, size); ++i)
    {
        b += key[a] + _sbox[i & 0xFF];
        iswap(_sbox[i & 0xFF], _sbox[b & 0xFF]);
        ++a;
        a %= size;
    }
}

void HPRC4LikeCipher::Apply(uint8 *buf, uint32 size)
{
    // remaining bytes from last round, to keep cipher consistent
    // this will probably invalidate proper memory alignment, though
    if(_rb)
    {
        if(_rb < size)
        {
            uint8 r = _rb;
            _DoRemainingBytes(buf, _rb);
            buf += r;
            size -= r;
        }
        else
        {
            _DoRemainingBytes(buf, (uint8)size);
            return;
        }
    }

    uint32 isize = size / 4;
    size -= (isize * 4); // remaining bytes

    uint32 *sbox = &_sbox[0];
    uint32 x = _x, y = _y;

    // process as much as possible as uint32 blocks
    // optimization: it seems that having x, y, t as uint32 and using & 0xFF everytime
    //               is more efficient then using uint8 and the fact that 0xFF overflows to 0x00
    if(isize)
    {
        uint32 *ibuf = (uint32*)buf;
        uint32 t;
        do
        {
            ++x;
            x &= 0xFF;
            y += sbox[x];
            y &= 0xFF;
            iswap(sbox[x], sbox[y]);
            t = sbox[x] + sbox[y];
            t &= 0xFF;
    #if IS_LITTLE_ENDIAN
            *ibuf++ ^= sbox[t];
    #else
            uint32 m = sbox[t];
            ToLittleEndian(m);
            *ibuf++ ^= m;
    #endif
            // leave lowest 8 bits intact (which are used for permutation), and mix the upper 24 bytes
            sbox[t] = (sbox[t] & 0xFF) | (0xFFFFFF00 & (sbox[t] ^ sbox[y] ^ sbox[x]));
        }
        while(--isize);

        buf = (uint8*)ibuf;
    }

    _x = (uint8)x;    
    _y = (uint8)y;

    // are there remaining bytes at the end of the buffer?
    if(size)
    {
        // we have to start a new round
        ++_x;
        _y += (uint8)sbox[_x];
        iswap(sbox[_x], sbox[_y]);
        _rb = sizeof(uint32); // after _DoRemainingBytes(), it will hold the amount of bytes to do in the next round
        _DoRemainingBytes(buf,(uint8)size);
    }
}

// slow
void HPRC4LikeCipher::_DoRemainingBytes(uint8 *buf, uint8 bytes)
{
    if(!bytes)
        return;
    DEBUG(ASSERT(bytes < sizeof(uint32)));
    uint8 t = uint8(_sbox[_x] + _sbox[_y]);

    union
    {
        uint8 byteval[4];
        uint32 intval;
    } u;

    u.intval = _sbox[t];

#if IS_LITTLE_ENDIAN
    ToBigEndian(u.intval);
#endif

    do
        *buf++ ^= u.byteval[--_rb];
    while(--bytes);

    DEBUG(ASSERT(_rb < sizeof(uint32)));

    if(!_rb)
    {
        // finish the uint32 round
        _sbox[t] = (_sbox[t] & 0xFF) | (0xFFFFFF00 & (_sbox[t] ^ _sbox[_y] ^ _sbox[_x]));
    }
}

LVPA_NAMESPACE_END
