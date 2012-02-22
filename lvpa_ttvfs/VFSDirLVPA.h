#ifndef VFSDIR_LVPA_H
#define VFSDIR_LVPA_H

#include "VFSDir.h"
#include "LVPACompileConfig.h"

LVPA_NAMESPACE_START
class LVPAFile;
LVPA_NAMESPACE_END

VFS_NAMESPACE_START

class VFSDirLVPA : public VFSDir
{
public:
    VFSDirLVPA(LVPA_NAMESPACE_IMPL LVPAFile *f, bool asSubdir);
    virtual ~VFSDirLVPA() {};
    virtual unsigned int load(const char *dir = NULL);
    virtual VFSDir *createNew(const char *dir) const;
    virtual const char *getType(void) const { return "VFSDirLVPA"; }

    inline LVPA_NAMESPACE_IMPL LVPAFile *getLVPA(void) { return _lvpa; }

protected:
    LVPA_NAMESPACE_IMPL LVPAFile *_lvpa;
};

VFS_NAMESPACE_END

#endif
