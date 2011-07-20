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

VFSDirLVPA::VFSDirLVPA(LVPAFile *f, bool asSubdir)
{
    _lvpa = f;
    _fullname = asSubdir ? f->GetMyName() : StripLastPath(f->GetMyName());
    _name = PathToFileName(_fullname.c_str());
}

VFSDir *VFSDirLVPA::createNew(void) const
{
    return new VFSDir; // inside an LVPA file; only the base dir can be a real VFSDirLVPA. (FIXME: is this really correct?)
}
/*
VFSDirLVPA *VFSDirLVPA::_getSubdir(const char *name)
{
    char *slashpos = (char *)strchr(name, '/');

    // if there is a '/' in the string, descend into subdir and continue there
    if(slashpos)
    {
        *slashpos = 0; // temp change to avoid excess string mangling
        const char *sub = slashpos + 1;

        Dirs::iterator it = _subdirs.find(name);
        VFSDirLVPA *subdir;
        if(it == _subdirs.end())
        {
            subdir = new VFSDirLVPA(_lvpa);
            subdir->_name = name;
            _subdirs[subdir->name()] = subdir;
        }
        else
            subdir = (VFSDirLVPA*)it->second;

        *slashpos = '/'; // restore original string

        return subdir->_getSubdir(sub);
    }

    // no more '/' in dir name, means the remaining string is the file name only, and the current dir is the one we want.
    return this;
}
*/
unsigned int VFSDirLVPA::load(const char *) // arg ignored
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
            continue;

        //VFSDirLVPA *subdir = _getSubdir(hdr.filename.c_str());
        //std::string sdname = StripLastPath(hdr.filename);
        //VFSDir *subdir = getDir(sdname.c_str(), true);
        VFSFileLVPA *file = new VFSFileLVPA(_lvpa, i);
        //logdebug("VFS/LVPA: subdir: '%s'; file: '%s'", subdir->fullname(), file->fullname());
        //subdir->add(file, true);
        addRecursive(file, true);
        file->ref--; // file was added and refcount increased, decref here
        ++ctr;
    }
    return ctr;
}

VFS_NAMESPACE_END
