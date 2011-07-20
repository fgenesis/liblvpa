#include "VFSHelperLVPA.h"
#include "VFSFileLVPA.h"
#include "VFSDirLVPA.h"
#include "VFSLoaderLVPA.h"

#include "LVPAFile.h"

#include "VFSInternal.h"
#include "LVPAInternal.h"

#ifdef LVPA_NAMESPACE
  using namespace LVPA_NAMESPACE;
#endif

VFS_NAMESPACE_START

VFSHelperLVPA::VFSHelperLVPA()
: lvpabase(NULL),lvpaRoot(NULL)
{
    _ldrLvpaId = _AddFixedLoader();
}

VFSHelperLVPA::~VFSHelperLVPA()
{
    if(lvpaRoot)
        lvpaRoot->ref--;
    if(lvpabase)
        delete lvpabase;
}

void VFSHelperLVPA::_cleanup(void)
{
    super::_cleanup();
    for(std::set<LVPAFile*>::iterator it = lvpalist.begin(); it != lvpalist.end(); it++)
        delete *it;
    lvpalist.clear();
}

void VFSHelperLVPA::LoadBaseContainer(LVPAFile *f, bool deleteLater)
{
    if(lvpaRoot)
        lvpaRoot->ref--;
    if(lvpabase)
    {
        delete lvpabase;
        lvpabase = NULL;
    }

    lvpaRoot = new VFSDirLVPA(f, false); // FIXME: check this!
    lvpaRoot->load();

    if(deleteLater)
        lvpabase = f;

    if(fixedLdrs[_ldrLvpaId])
    {
        delete fixedLdrs[_ldrLvpaId];
        fixedLdrs[_ldrLvpaId] = NULL;
    }

    // if the container has scrambled files, register a loader.
    // we can't add scrambled files to the tree, because their names are probably unknown at this point
    for(uint32 i = 0; i < f->HeaderCount(); ++i)
    {
        if(f->GetFileInfo(i).flags & LVPAFLAG_SCRAMBLED)
        {
            fixedLdrs[_ldrLvpaId] = new VFSLoaderLVPA(f);
            break;
        }
    }
}

bool VFSHelperLVPA::AddContainer(LVPAFile *f, const char *path, bool deleteLater,
                                 bool asSubdir /* = true */, bool overwrite /* = true */)
{
    VFSDirLVPA *vfs = new VFSDirLVPA(f, asSubdir);
    if(vfs->load())
    {
        AddVFSDir(vfs, path, overwrite);
        if(deleteLater)
            lvpalist.insert(f);

        // if the container has scrambled files, register a loader.
        // we can't add scrambled files to the tree, because their names are probably unknown at this point
        for(uint32 i = 0; i < f->HeaderCount(); ++i)
        {
            if(f->GetFileInfo(i).flags & LVPAFLAG_SCRAMBLED)
            {
                dynLdrs.push_back(new VFSLoaderLVPA(f));
                break;
            }
        }
    }
    else if(deleteLater)
        delete f; // loading unsucessful, delete now

    return !!--(vfs->ref); // 0 if if deleted
}


VFS_NAMESPACE_END
