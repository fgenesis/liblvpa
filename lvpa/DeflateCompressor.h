
#ifndef DEFLATE_COMPRESSOR_H
#define DEFLATE_COMPRESSOR_H

#include "ICompressor.h"


LVPA_NAMESPACE_START

// implements a raw deflate stream, not zlib wrapped, and not checksummed.
class DeflateCompressor : public ICompressor
{
public:
    DeflateCompressor();
    virtual ~DeflateCompressor() {}
    virtual void Compress(uint8 level = 1, ProgressCallback pcb = NULL);
    virtual void Decompress(void);

protected:
    int _windowBits; // read zlib docs to know what this means
    bool _forceCompress;

private:
    static void decompress(void *dst, uint32 *origsize, const void *src, uint32 size, int wbits);
    static void compress(void* dst, uint32 *dst_size, const void* src, uint32 src_size,
        uint8 level, int wbits, ProgressCallback pcb);
};

// implements deflate stream, zlib wrapped
class ZlibCompressor : public DeflateCompressor
{
public:
    ZlibCompressor();
    virtual ~ZlibCompressor() {}
};

// the output produced by this stream contains a minimal gzip header,
// and can be directly written to a .gz file.
class GzipCompressor : public DeflateCompressor
{
public:
    GzipCompressor();
    virtual ~GzipCompressor() {}
    virtual void Decompress(void);
};

LVPA_NAMESPACE_END

#endif
