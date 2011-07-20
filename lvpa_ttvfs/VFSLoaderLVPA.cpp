#include "VFSLoaderLVPA.h"
#include "VFSFileLVPA.h"
#include "LVPAFile.h"

#include "LVPACommon.h"


VFS_NAMESPACE_START

VFSLoaderLVPA::VFSLoaderLVPA(LVPA_NAMESPACE_IMPL LVPAFile *lvpa)
: _lvpa(lvpa)
{
}

VFSFile *VFSLoaderLVPA::Load(const char *fn)
{
    unsigned int id = _lvpa->GetId(fn); // this will also trigger file name indexing in case this file is scrambled, but the name correct
    if(id == -1)
        return NULL; // no such file

    return new VFSFileLVPA(_lvpa, id); // because its indexed now, loading it this way will work fine
}

VFS_NAMESPACE_END
