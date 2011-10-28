#include "LVPACompileConfig.h"

#ifdef LVPA_SUPPORT_ZLIB

#include <zlib/zutil.h>
#include <zlib/zlib.h>

#include "LVPAInternal.h"
#include "DeflateCompressor.h"

// for weird gcc/mingw hackfix below
#include <string.h>

LVPA_NAMESPACE_START

DeflateCompressor::DeflateCompressor()
:   _windowBits(-MAX_WBITS), // negative, because we want a raw deflate stream, and not zlib-wrapped
    _forceCompress(false)
{
}

ZlibCompressor::ZlibCompressor()
: DeflateCompressor()
{
    _windowBits = MAX_WBITS; // positive, means we use a zlib-wrapped deflate stream
}

GzipCompressor::GzipCompressor()
: DeflateCompressor()  
{
    _windowBits = MAX_WBITS + 16; // this makes zlib wrap a minimal gzip header around the stream
    _forceCompress = true; // we want this for gzip
}

// TODO: do compression block-wise, and call progress callback periodically
void DeflateCompressor::compress(void* dst, uint32 *dst_size, const void* src, uint32 src_size,
                                 uint8 level, int wbits, ProgressCallback pcb)
{
    z_stream c_stream;

    c_stream.zalloc = (alloc_func)Z_NULL;
    c_stream.zfree = (free_func)Z_NULL;
    c_stream.opaque = (voidpf)Z_NULL;

    if (Z_OK != deflateInit2(&c_stream, level, Z_DEFLATED, wbits, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY))
    {
        //logerror("ZLIB: Can't compress (zlib: deflateInit).\n");
        *dst_size = 0;
        return;
    }

    c_stream.next_out = (Bytef*)dst;
    c_stream.avail_out = *dst_size;
    c_stream.next_in = (Bytef*)src;
    c_stream.avail_in = (uInt)src_size;

    if (Z_OK != deflate(&c_stream, Z_NO_FLUSH))
    {
        logerror("ZLIB: Can't compress (zlib: deflate)");
        *dst_size = 0;
        return;
    }

    if (c_stream.avail_in != 0)
    {
        logerror("Can't compress (zlib: deflate not greedy)");
        *dst_size = 0;
        return;
    }

    if (Z_STREAM_END != deflate(&c_stream, Z_FINISH))
    {
        logerror("Can't compress (zlib: deflate, finish)");
        *dst_size = 0;
        return;
    }

    if (Z_OK != deflateEnd(&c_stream))
    {
        logerror("Can't compress (zlib: deflateEnd)");
        *dst_size = 0;
        return;
    }

    *dst_size = c_stream.total_out;
}

void DeflateCompressor::decompress(void *dst, uint32 *origsize, const void *src, uint32 size, int wbits)
{
    z_stream stream;
    int err;

    stream.next_in = (Bytef*)src;
    stream.avail_in = (uInt)size;
    stream.next_out = (Bytef*)dst;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    err = inflateInit2(&stream, wbits);
    if (err != Z_OK)
    {
        *origsize = 0;
        return;
    }

    err = inflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END)
    {
        inflateEnd(&stream);
        *origsize = 0;
    }
    *origsize = (uint32)stream.total_out;

    err = inflateEnd(&stream);
    if(err != Z_OK)
        *origsize = 0;
}


void DeflateCompressor::Compress(uint8 level, ProgressCallback pcb /* = NULL */)
{
    if(!_forceCompress && (!level || _iscompressed || (!size())))
        return;

    char *buf;
    

    uint32 oldsize = size();
    uint32 newsize = compressBound(oldsize) + 30; // for optional gzip header

    buf = new char[newsize];

    compress((void*)buf, &newsize, (void*)contents(), oldsize, level, _windowBits, pcb);
    if(!newsize || (!_forceCompress && newsize > oldsize)) // only allow more data if compression is forced (which is the case for gzip)
    {
        delete [] buf;
        return;
    }

    resize(newsize);
    rpos(0);
    wpos(0);
    append(buf,newsize);
    delete [] buf;

    _iscompressed = true;

    _real_size = oldsize;
}

void DeflateCompressor::Decompress(void)
{
    if( (!_iscompressed) || (!_real_size) || (!size()))
        return;

    uint32 rs = (uint32)_real_size;
    uint32 origsize = rs;
    uint8 *target = new uint8[rs];
    wpos(0);
    rpos(0);
    decompress((void*)target, &origsize, (const void*)contents(), size(), _windowBits);
    if(origsize != rs)
    {
        logerror("DeflateCompressor: Inflate error! result=%d cursize=%u origsize=%u realsize=%u",size(),origsize,rs);
        delete [] target;
        return;
    }
    clear();
    append(target, origsize);
    delete [] target;
    _real_size = 0;
    _iscompressed = false;

}

void GzipCompressor::Decompress(void)
{
    uint32 t = 0;
    rpos(size() - sizeof(uint32)); // according to RFC 1952, input size are the last 4 bytes at the end of the file, in little endian
    *this >> t;
    _real_size = t;
    
    // !! NOTE: this fixes a gcc/mingw bug where _real_size would be set incorrectly
#if COMPILER == COMPILER_GNU
    char xx[20];
    sprintf(xx, "%u", t);
#endif

    DeflateCompressor::Decompress(); // will set rpos back anyway
}

LVPA_NAMESPACE_END

#endif // LVPA_SUPPORT_ZLIB
