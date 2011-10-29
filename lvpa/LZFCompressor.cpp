#include "LVPACompileConfig.h"
#ifdef LVPA_SUPPORT_LZF

#include "lzf/lzf.h"
#include "LVPAInternal.h"
#include "LZFCompressor.h"

LVPA_NAMESPACE_START


void LZFCompressor::Compress(uint8 /*level*/, ProgressCallback pcb /* = NULL */) // both not used
{
    if(_iscompressed || (!size()))
        return;

    unsigned int oldsize = (unsigned int)size();
    unsigned int newsize = oldsize - 1;

    uint8 *buf = new uint8[newsize];

    newsize = lzf_compress(contents(), oldsize, buf, newsize);

    if(!newsize)
    {
        //DEBUG(logdebug("LZFCompressor: This block contains incompressible data."));
        delete [] buf;
        return;
    }

    resize((size_t)newsize);
    rpos(0);
    wpos(0);
    append(buf,(size_t)newsize);
    delete [] buf;

    _iscompressed = true;
    _real_size = (uint64)oldsize;

    if(pcb)
        pcb(NULL, oldsize, newsize);
}

void LZFCompressor::Decompress(void)
{
    if( (!_iscompressed) || (!_real_size) || (!size()))
        return;

    unsigned int currentSize = size();
    unsigned int targetSize = _real_size;
    uint8 *target = new uint8[targetSize];

    targetSize = lzf_decompress(contents(), currentSize, target, targetSize);
    if(!targetSize)
    {
        logerror("LZFCompressor: decompression failed");
        return;
    }

    resize((size_t)targetSize);
    wpos(0);
    rpos(0);
    append(target, (size_t)targetSize);
    delete [] target;
    _real_size = 0;
    _iscompressed = false;
}

LVPA_NAMESPACE_END

#endif // LVPA_SUPPORT_LZO
