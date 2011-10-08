#include "LVPACompileConfig.h"
#ifdef LVPA_SUPPORT_LZHAM

#include <stddef.h> // was forgotten in lzham
#include "lzham/lzham_static_lib.h"
#include "LVPAInternal.h"
#include "LVPATools.h"
#include "LZHAMCompressor.h"

LVPA_NAMESPACE_START


void LZHAMCompressor::Compress(uint8 level, ProgressCallback pcb /* = NULL */)
{
    if(_iscompressed || (!size()))
        return;

    // to be honest, i am not exactly sure if the params auto-selected here are okay...
    // just guessing, for now

    lzham_compress_params comp_params;
    memset(&comp_params, 0, sizeof(comp_params));
    comp_params.m_struct_size = sizeof(comp_params);
    comp_params.m_max_helper_threads = 1; // FIXME: really?


    switch(level)
    {
        case 0:
        case 1:
        case 2:
            comp_params.m_level = LZHAM_COMP_LEVEL_FASTEST;
            break;
        case 3:
        case 4:
            comp_params.m_level = LZHAM_COMP_LEVEL_FASTER;
            break;
        case 5:
        case 6:
            comp_params.m_level = LZHAM_COMP_LEVEL_DEFAULT;
            break;
        case 7:
        case 8:
            comp_params.m_level = LZHAM_COMP_LEVEL_BETTER;
            break;
        case 9:
            comp_params.m_level = LZHAM_COMP_LEVEL_UBER;
    }

    // this gives 26 on level 9, which is ~128 MB
    // (26 is max. recommended on x86/32bit)
    comp_params.m_dict_size_log2 = lzham_compress_level(level + 17);

    size_t oldsize = size();

    // limit dict size to sane values (at least try to)
    // e.g. do not allocate more memory for the dict than half the file size
    if(oldsize <= 0xFFFFFFFF)
    {
        uint32 logbase = std::max<uint32>(ilog2(oldsize >> 1), LZHAM_MIN_DICT_SIZE_LOG2);
        if(comp_params.m_dict_size_log2 > logbase)
            comp_params.m_dict_size_log2 = logbase;
    }

    if(level != 9 && level & 1)
        comp_params.m_compress_flags |= LZHAM_COMP_FLAG_FORCE_POLAR_CODING;

    
    size_t newsize = oldsize - 1;
    lzham_uint32 adler; // unused


    uint8 *buf = new uint8[newsize];

    lzham_compress_status_t status = lzham_compress_memory(&comp_params, buf, &newsize, contents(), oldsize, &adler);

    if(status != LZHAM_COMP_STATUS_SUCCESS || !newsize)
    {
        delete [] buf;
        return;
    }

    resize((size_t)newsize);
    rpos(0);
    wpos(0);
    (*this) << uint8(comp_params.m_dict_size_log2);
    append(buf,(size_t)newsize);
    delete [] buf;

    _iscompressed = true;
    _real_size = (uint64)oldsize;
}

void LZHAMCompressor::Decompress(void)
{
    if( (!_iscompressed) || (!_real_size) || (!size()))
        return;

    const uint8 *readbuf = contents();
    uint8 dictsize = *readbuf++; // first byte in stream

    if(dictsize < LZHAM_MIN_DICT_SIZE_LOG2 || dictsize > LZHAM_MAX_DICT_SIZE_LOG2_X64)
        return;

    size_t currentSize = size() - 1; // skipped first byte
    size_t targetSize = _real_size;
    uint8 *target = new uint8[targetSize];
    lzham_uint32 adler; // unused
    
    lzham_decompress_params decomp_params;
    memset(&decomp_params, 0, sizeof(decomp_params));
    decomp_params.m_struct_size = sizeof(decomp_params);
    decomp_params.m_dict_size_log2 = dictsize;
    decomp_params.m_compute_adler32 = false; // not needed, doing own crc32 checking after decompressing
    decomp_params.m_output_unbuffered = true; // FIXME: not sure if this is really okay for big files

    lzham_decompress_status_t status = lzham_decompress_memory(&decomp_params, target, &targetSize, readbuf, currentSize, &adler);

    if(status != LZHAM_DECOMP_STATUS_SUCCESS || !targetSize)
    {
        logerror("LZHAMCompressor: decompression failed");
        return;
    }

    resize(targetSize);
    wpos(0);
    rpos(0);
    append(target, targetSize);
    delete [] target;
    _real_size = 0;
    _iscompressed = false;
}

LVPA_NAMESPACE_END

#endif // LVPA_SUPPORT_LZO
