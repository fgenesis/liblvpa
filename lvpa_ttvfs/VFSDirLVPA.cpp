#include "LVPAFile.h"
#include "VFSFileLVPA.h"
#include "VFSDirLVPA.h"
#include "VFSTools.h"

#include "VFSInternal.h"
#include "LVPAInternal.h"

VFS_NAMESPACE_START

#ifdef LVPA_NAMESPACE
  using namespace LVPA_NAMESPACE;
#endif

VFSDirLVPA::VFSDirLVPA(VFSFile *vf, LVPAFile *lvpa, bool asSubdir)
: VFSDir(asSubdir ? vf->fullname() : StripLastPath(vf->fullname()).c_str()), _lvpa(lvpa), _origin(vf)
{
    _origin->ref++;
}

VFSDirLVPA::~VFSDirLVPA()
{
    delete _lvpa; // TODO: possibility to keep the archive alive anyway?

    // Must be done *after* deleting the LVPA file!
    _origin->ref--;
}

VFSDir *VFSDirLVPA::createNew(const char *dir) const
{
    return new VFSDir(dir); // inside an LVPA file; only the base dir can be a real VFSDirLVPA.
}

unsigned int VFSDirLVPA::load(bool /*ignored*/)
{
    unsigned int ctr = 0;
    for(uint32 i = 0; i < _lvpa->HeaderCount(); i++)
    {
        const LVPAFileHeader& hdr = _lvpa->GetFileInfo(i);
        if(!hdr.good)
        {
            logerror("VFSFileLVPA::load(): Corrupt file '%s'", hdr.filename.c_str());
            continue;
        }
        // solid blocks are no real files, and scrambled files without filename can't be read anyways, at this point
        if(hdr.flags & LVPAFLAG_SOLIDBLOCK || (hdr.flags & LVPAFLAG_SCRAMBLED && hdr.filename.empty()))
        {
            ++ctr;
            continue;
        }

        VFSFileLVPA *file = new VFSFileLVPA(_lvpa, i);
        addRecursive(file, true);
        file->ref--; // file was added and refcount increased, decref here
        ++ctr;
    }
    return ctr;
}

void VFSDirLVPA::clearGarbage(void)
{
    _lvpa->FreeUnused();
}

VFS_NAMESPACE_END
