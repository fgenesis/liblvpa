#include "LVPACompileConfig.h"

#ifdef LVPA_SUPPORT_LZMA

#include <lzma/LzmaDec.h>
#include <lzma/LzmaEnc.h>
#include "LVPAInternal.h"
#include "LZMACompressor.h"

LVPA_NAMESPACE_START

static SRes myLzmaProgressDummy(void *, UInt64 , UInt64 )
{
    return SZ_OK;
}

static void *myLzmaAlloc(void *, size_t size)
{
    return malloc(size);
}

static void myLzmaFree(void *, void *ptr)
{
    if(ptr)
        free(ptr);
}


void LZMACompressor::Compress(uint8 level, ProgressCallback pcb /* = NULL */)
{
    if( _iscompressed || (!size()))
        return;

    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = level;

    SizeT oldsize = size();
    SizeT newsize = oldsize / 20 * 21 + (1 << 16); // we allocate 105% of original size for output buffer

    Byte *buf = new Byte[newsize];
    

    ISzAlloc alloc;
    alloc.Alloc = myLzmaAlloc;
    alloc.Free = myLzmaFree;

    ICompressProgress progress;
    progress.Progress = pcb ? pcb : myLzmaProgressDummy;

    SizeT propsSize = LZMA_PROPS_SIZE;
    Byte propsEnc[LZMA_PROPS_SIZE];

    uint32 result = LzmaEncode(buf, &newsize, this->contents(), oldsize, &props, &propsEnc[0], &propsSize, 0, &progress, &alloc, &alloc);
    if(result != SZ_OK || !newsize || newsize > oldsize)
    {
        delete [] buf;
        return;
    }
    ASSERT(propsSize == LZMA_PROPS_SIZE); // this should not be changed by the library

    resize(newsize);
    rpos(0);
    wpos(0);
    append(&propsEnc[0], LZMA_PROPS_SIZE);
    append(buf,newsize);
    delete [] buf;

    _iscompressed = true;

    _real_size = oldsize;
}

void LZMACompressor::Decompress(void)
{
    if( (!_iscompressed) || (!_real_size) || (!size()))
        return;

    uint32 origsize = _real_size;
    int8 result = SZ_OK;
    uint8 *target = new uint8[_real_size];
    wpos(0);
    rpos(0);
    SizeT srcLen = this->size() - LZMA_PROPS_SIZE;

    SizeT propsSize = LZMA_PROPS_SIZE;
    Byte propsEnc[LZMA_PROPS_SIZE];
    read(&propsEnc[0], LZMA_PROPS_SIZE);

    ISzAlloc alloc;
    alloc.Alloc = myLzmaAlloc;
    alloc.Free = myLzmaFree;

    ELzmaStatus status;
    Byte *dataPtr = (Byte*)this->contents() + LZMA_PROPS_SIZE; // first 5 bytes are encoded props

    result = LzmaDecode(target, (SizeT*)&_real_size, dataPtr, &srcLen, &propsEnc[0], propsSize, LZMA_FINISH_END, &status, &alloc);
    if( result != SZ_OK || origsize != _real_size)
    {
        //DEBUG(logerror("LZMACompressor: Decompress error! result=%d cursize=%u origsize=%u realsize=%u\n",result,size(),origsize,_real_size));
        delete [] target;
        return;
    }
    ASSERT(propsSize == LZMA_PROPS_SIZE); // this should not be changed by the library
    resize(origsize);
    wpos(0);
    rpos(0);
    append(target, origsize);
    delete [] target;
    _real_size = 0;
    _iscompressed = false;
}

LVPA_NAMESPACE_END

#endif // LVPA_SUPPORT_LZMA
