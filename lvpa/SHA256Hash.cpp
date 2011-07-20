#include "LVPAInternal.h"
#include "SHA256Hash.h"

LVPA_NAMESPACE_START


SHA256Hash::SHA256Hash(uint8 *target /* = NULL */)
: _digestPtr(target ? target : &_digest[0])
{
    sha256_init(&_ctx);
}

void SHA256Hash::Update(const uint8 *ptr, uint32 size)
{
    sha256_chunk(&_ctx, ptr, size);
}

void SHA256Hash::Finalize(void)
{
    sha256_final(&_ctx);
    sha256_hash(&_ctx, _digestPtr);
}

void SHA256Hash::Calc(uint8 *dst, const uint8 *src, uint32 size)
{
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_chunk(&ctx, src, size);
    sha256_final(&ctx);
    sha256_hash(&ctx, dst);
}

LVPA_NAMESPACE_END
