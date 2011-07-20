#ifndef WHIRLPOOL_HASH_H
#define WHIRLPOOL_HASH_H

#include "sha256.h"

LVPA_NAMESPACE_START

#define SHA256_DIGEST_LENGTH 32

class SHA256Hash
{
public:
    SHA256Hash(uint8 *target = NULL);
    void Update(const uint8 *ptr, uint32 size);
    void Finalize(void);
    const uint8 *GetDigest(void) const { return &_digest[0]; }

    static inline uint32 Size(void) { return SHA256_DIGEST_LENGTH; }
    static void Calc(uint8 *dst, const uint8 *src, uint32 size);

private:
    sha256_ctx _ctx;
    uint8 *_digestPtr;
    uint8 _digest[SHA256_DIGEST_LENGTH];
};

LVPA_NAMESPACE_END

#endif