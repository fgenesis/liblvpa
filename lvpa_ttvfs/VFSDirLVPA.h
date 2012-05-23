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
    VFSDirLVPA(VFSFile *vf, LVPA_NAMESPACE_IMPL LVPAFile *f);
    virtual ~VFSDirLVPA();
    virtual unsigned int load(bool recursive); // recursive is ingored
    virtual VFSDir *createNew(const char *dir) const;
    virtual const char *getType() const { return "VFSDirLVPA"; }
    virtual void clearGarbage();
    virtual bool close();

    inline LVPA_NAMESPACE_IMPL LVPAFile *getLVPA() { return _lvpa; }

protected:
    LVPA_NAMESPACE_IMPL LVPAFile *_lvpa;
    VFSFile *_vlvpa;
};

VFS_NAMESPACE_END

#endif
