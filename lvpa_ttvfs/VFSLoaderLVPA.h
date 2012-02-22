#ifndef VFSLOADER_LVPA_H
#define VFSLOADER_LVPA_H

#include "LVPACompileConfig.h"
#include "VFSLoader.h"

LVPA_NAMESPACE_START
class LVPAFile;
LVPA_NAMESPACE_END

VFS_NAMESPACE_START

class VFSFile;

class VFSLoaderLVPA : public VFSLoader
{
public:
    VFSLoaderLVPA(LVPA_NAMESPACE_IMPL LVPAFile *lvpa);
    virtual ~VFSLoaderLVPA() {}
    virtual VFSFile *Load(const char *fn, const char *unmangled);

protected:
    LVPA_NAMESPACE_IMPL LVPAFile *_lvpa;
};

VFS_NAMESPACE_END

#endif
