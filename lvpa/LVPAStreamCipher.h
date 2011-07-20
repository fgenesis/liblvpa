#ifndef LVPA_STREAM_CIPHER_H
#define LVPA_STREAM_CIPHER_H

LVPA_NAMESPACE_START

// symmetric stream cipher - use 1 instance for encryption, 1 for decryption
class ISymmetricCipher
{
public:
    virtual ~ISymmetricCipher() {}
    virtual void Init(const uint8 *key, uint32 size) {}
    virtual void Apply(uint8 *buf, uint32 size) = 0;
    virtual void WarmUp(uint32 size);
};

// the original (A)RC4 algorithm - for reference
class RC4Cipher : public ISymmetricCipher
{
public:
    RC4Cipher();
    virtual ~RC4Cipher() {}
    virtual void Init(const uint8 *key, uint32 size);
    virtual void Apply(uint8 *buf, uint32 size);

private:
    uint8 _sbox[256];
    uint8 _x, _y;
};

// Improved higher-performance RC4-like algorithm
// Speed is about 2-3x as fast as the reference RC4 above, on little endian.
// For best speed, make sure the actual buffer size is always a multiple of 4, and properly aligned.
class HPRC4LikeCipher : public ISymmetricCipher
{
public:
    HPRC4LikeCipher();
    virtual ~HPRC4LikeCipher() {}
    virtual void Init(const uint8 *key, uint32 size);
    virtual void Apply(uint8 *buf, uint32 size);
    void _DoRemainingBytes(uint8 *buf, uint8 bytes);

private:
    uint32 _sbox[256];
    uint8 _x, _y, _rb;
};

LVPA_NAMESPACE_END

#endif
