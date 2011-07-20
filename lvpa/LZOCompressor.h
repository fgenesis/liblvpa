
#ifndef _LZOCOMPRESSOR_H
#define _LZOCOMPRESSOR_H

#include "ICompressor.h"

LVPA_NAMESPACE_START

class LZOCompressor : public ICompressor
{
public:
    virtual void Compress(uint8 level = 1, ProgressCallback pcb = NULL);
    virtual void Decompress(void);

private:
    static bool s_lzoNeedsInit;
};

LVPA_NAMESPACE_END

#endif
