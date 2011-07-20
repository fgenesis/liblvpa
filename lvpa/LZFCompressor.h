
#ifndef _LZFCOMPRESSOR_H
#define _LZFCOMPRESSOR_H

#include "ICompressor.h"

LVPA_NAMESPACE_START

class LZFCompressor : public ICompressor
{
public:
    virtual void Compress(uint8 level = 0, ProgressCallback pcb = NULL); // both args unused
    virtual void Decompress(void);

private:
    static bool s_lzoNeedsInit;
};

LVPA_NAMESPACE_END

#endif
