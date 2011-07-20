// code taken from phoenix - by byuu (http://byuu.org)

#ifndef SHA256_H
#define SHA256_H

LVPA_NAMESPACE_START

//author: vladitx

struct sha256_ctx
{
    uint8 in[64];
    unsigned inlen;

    uint32 w[64];
    uint32 h[8];
    uint64 len;
};

void sha256_init(sha256_ctx *p);
void sha256_chunk(sha256_ctx *p, const uint8 *s, uint32 len);
void sha256_final(sha256_ctx *p);
void sha256_hash(sha256_ctx *p, uint8 *s);

LVPA_NAMESPACE_END

#endif
