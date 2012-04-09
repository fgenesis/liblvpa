#ifndef VFS_ZIP_ARCHIVE_LOADER_H
#define VFS_ZIP_ARCHIVE_LOADER_H

#include "VFSArchiveLoader.h"

VFS_NAMESPACE_START

class VFSLVPAArchiveLoader : public VFSArchiveLoader
{
public:
    virtual ~VFSLVPAArchiveLoader() {}
    virtual VFSDir *Load(VFSFile *arch, bool asSubdir, VFSLoader **ldr, void *opaque = NULL);
};

// used as 'opaque' pointer - see VFSArchiveLoader docs
struct LVPALoadParams
{
    void (*callback)(void *, void *, const char *);
    const void *key;
    unsigned int keylen;
};


VFS_NAMESPACE_END

#endif
