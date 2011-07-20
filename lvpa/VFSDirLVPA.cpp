#include "LVPACommon.h"
#include "LVPAFile.h"
#include "VFSFileLVPA.h"
#include "VFSDirLVPA.h"

using namespace ttvfs;

VFSDirLVPA::VFSDirLVPA(LVPAFile *f)
{
    _lvpa = f;
    _fullname = f->GetMyName();
    _name = PathToFileName(_fullname.c_str());
}

VFSDirLVPA *VFSDirLVPA::_getSubdir(const char *name)
{
    char *slashpos = (char *)strchr(name, '/');

    // if there is a '/' in the string, descend into subdir and continue there
    if(slashpos)
    {
        *slashpos = 0; // temp change to avoid excess string mangling
        const char *sub = slashpos + 1;

        VFSDirMap::iterator it = _subdirs.find(name);
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

        DEBUG(ASSERT(subdir != NULL));
        return subdir->_getSubdir(sub);
    }

    // no more '/' in dir name, means the remaining string is the file name only, and the current dir is the one we want.
    return this;
}

uint32 VFSDirLVPA::load(const char *, bool ignoreCase /* = false */) // first arg unused
{
    uint32 ctr = 0;
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

        VFSDirLVPA *subdir = _getSubdir(hdr.filename.c_str());
        VFSFileLVPA *file = new VFSFileLVPA(_lvpa, i);
        subdir->add(file, true);
        file->ref--; // file was added and refcount increased, decref here
        ++ctr;
    }
    return ctr;
}
