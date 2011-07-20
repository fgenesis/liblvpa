#include "LVPACompileConfig.h"
#ifdef LVPA_SUPPORT_LZO

#include <lzo/lzo1x.h>
#include "LVPAInternal.h"
#include "LZOCompressor.h"

LVPA_NAMESPACE_START

/* Work-memory needed for compression. Allocate memory in units
* of `lzo_align_t' (instead of `char') to make sure it is properly aligned.
*/
#define LZO_STACK_ALLOC(var,size) \
    lzo_align_t __LZO_MMODEL var [ ((size) + (sizeof(lzo_align_t) - 1)) / sizeof(lzo_align_t) ]
#define LZO_OUT_LEN(x)     ((x) + ((x) / 16) + 64 + 3)


bool LZOCompressor::s_lzoNeedsInit = true;

static void lzo_progress_wrapper(lzo_callback_p lzo_cb, lzo_uint cur, lzo_uint total, int)
{
    if(ICompressor::ProgressCallback my_cb = (ICompressor::ProgressCallback)(lzo_cb->user1))
        my_cb(NULL, cur, total);
}

void LZOCompressor::Compress(uint8 level, ProgressCallback pcb /* = NULL */)
{
    if(!level || _iscompressed || (!size()))
        return;

    if(s_lzoNeedsInit)
    {
        s_lzoNeedsInit = false;
        int res = lzo_init();
        ASSERT(res == LZO_E_OK);
    }
    
    lzo_uint oldsize = size();
    lzo_uint newsize = LZO_OUT_LEN(oldsize);
    
    uint8 *wrkmem = new uint8[LZO1X_999_MEM_COMPRESS];

    uint8 *buf = new uint8[newsize];

    lzo_callback_t cb;
    cb.nalloc = NULL;
    cb.nfree = NULL;
    cb.nprogress = lzo_progress_wrapper;
    cb.user1 = (void*)pcb;

    int r = lzo1x_999_compress_level(contents(), oldsize, buf, &newsize, wrkmem, NULL, 0, &cb, level);

    delete [] wrkmem;

    if(r != LZO_E_OK)
    {
        /* this should NEVER happen */
        logerror("LZOCompressor: internal error - compression failed: %d", r);
        delete [] buf;
        return;
    }
    /* check for an incompressible block */
    if (newsize >= oldsize)
    {
        //DEBUG(logdebug("LZOCompressor: This block contains incompressible data."));
        delete [] buf;
        return;
    }

    // TODO: add lzo1x_optimize() step and use this instead of append() ?

    resize((size_t)newsize);
    rpos(0);
    wpos(0);
    append(buf,(size_t)newsize);
    delete [] buf;

    _iscompressed = true;
    _real_size = (uint64)oldsize;
}

void LZOCompressor::Decompress(void)
{
    if( (!_iscompressed) || (!_real_size) || (!size()))
        return;

    if(s_lzoNeedsInit)
    {
        s_lzoNeedsInit = false;
        int res = lzo_init();
        ASSERT(res == LZO_E_OK);
    }

    lzo_uint currentSize = size();
    lzo_uint targetSize = _real_size;
    uint8 *target = new uint8[targetSize];
    
    int r = lzo1x_decompress(contents(), currentSize, target, &targetSize, NULL);
    if (!(r == LZO_E_OK && (uint64)targetSize == _real_size))
    {
        /* this should NEVER happen */
        logerror("LZOCompressor: internal error - decompression failed: %d", r);
        return;
    }

    resize((size_t)_real_size);
    wpos(0);
    rpos(0);
    append(target, (size_t)_real_size);
    delete [] target;
    _real_size = 0;
    _iscompressed = false;
}

LVPA_NAMESPACE_END

#endif // LVPA_SUPPORT_LZO
