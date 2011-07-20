
#ifndef _LZMACOMPRESSOR_H
#define _LZMACOMPRESSOR_H

#include "ICompressor.h"

LVPA_NAMESPACE_START

class LZMACompressor : public ICompressor
{
public:
    virtual void Compress(uint8 level = 1, ProgressCallback pcb = NULL);
    virtual void Decompress(void);
};

LVPA_NAMESPACE_END

#endif
