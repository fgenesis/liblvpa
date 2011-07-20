#ifndef VFS_HELPER_LVPA_H
#define VFS_HELPER_LVPA_H

#include "LVPACommon.h"
#include "VFSHelper.h"

LVPA_NAMESPACE_START
class LVPAFile;
LVPA_NAMESPACE_END


VFS_NAMESPACE_START

class VFSDirLVPA;

class VFSHelperLVPA : public VFS_LAST_HELPER_CLASS
{
private:
    typedef VFS_LAST_HELPER_CLASS super;

public:

    VFSHelperLVPA();
    virtual ~VFSHelperLVPA();

    void LoadBaseContainer(LVPA_NAMESPACE_IMPL LVPAFile *f, bool deleteLater);
    bool AddContainer(LVPA_NAMESPACE_IMPL LVPAFile *f, const char *subdir, bool deleteLater, bool asSubdir = true, bool overwrite = true);

protected:

    virtual void _cleanup(void);

    std::set<LVPA_NAMESPACE_IMPL LVPAFile*> lvpalist; // LVPA files delayed for deletion
    LVPA_NAMESPACE_IMPL LVPAFile *lvpabase; // base LVPA container
    VFSDirLVPA *lvpaRoot; // contains all files from lvpabase

private:
    unsigned int _ldrLvpaId;

};

VFS_NAMESPACE_END

#endif
