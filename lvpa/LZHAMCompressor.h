
#ifndef _LZHAMCOMPRESSOR_H
#define _LZHAMCOMPRESSOR_H

#include "ICompressor.h"

LVPA_NAMESPACE_START

class LZHAMCompressor : public ICompressor
{
public:
    virtual void Compress(uint8 level = 0, ProgressCallback pcb = NULL);
    virtual void Decompress(void);
};

LVPA_NAMESPACE_END

#endif
