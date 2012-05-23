#include "VFSInternal.h"
#include "VFSLVPAArchiveLoader.h"
#include "VFSDirLVPA.h"
#include "VFSLoaderLVPA.h"
#include "VFSFile.h"
#include "LVPAFile.h"

VFS_NAMESPACE_START

#ifdef LVPA_NAMESPACE
  using namespace LVPA_NAMESPACE;
#endif

static size_t lvpa_read_func(void *p, void *buf, size_t offs, size_t bytes)
{
    VFSFile *vf = (VFSFile*)p;
    vfspos curOffs = vf->getpos();
    if(curOffs != (vfspos)offs && !vf->seek((vfspos)offs))
        return 0;
    return vf->read(buf, bytes);
}

static void lvpa_close_func(void *p)
{
    VFSFile *vf = (VFSFile*)p;
    vf->close();
    vf->dropBuf(true);
}

static bool lvpa_open_func(const char * /*ignored*/, void *p)
{
    VFSFile *vf = (VFSFile*)p;
    return vf->open("rb");
}

VFSDir *VFSLVPAArchiveLoader::Load(VFSFile *vf, VFSLoader **ldr, void *opaque /* = NULL */)
{
    char buf[4];
    vf->open("rb");
    vf->read(buf, 4);
    vf->close();
    if(memcmp(&buf[0], "LVPA", 4)) // Quick rejection.
        return NULL;

    LVPAFileReader rd;
    rd.readF = lvpa_read_func;
    rd.closeF = lvpa_close_func;
    rd.openF = lvpa_open_func;
    rd.io = NULL;
    rd.opaque = vf;
    // no refcounting for vf here. We know that the reader lives as long as the LVPAFile object,
    // which gets deleted once the holding VFSDirLVPA object gets deleted. So we increase ref there.

    LVPAFile *lvpa = new LVPAFile;

    // Set password before attempting to actually load it.
    if(opaque)
    {
        LVPALoadParams *params = (LVPALoadParams*)opaque;
        params->callback(opaque, lvpa, "LVPA");
    }
    if(!lvpa->LoadFrom(vf->fullname(), &rd))
    {
        delete lvpa;
        return NULL;
    }

    VFSDirLVPA *vd = new VFSDirLVPA(vf, lvpa);

    if(vd->load(true))
    {
        if(ldr)
        {
            // if the container has scrambled files, register a loader.
            // we can't add scrambled files to the tree, because their names are unknown at this point.
            for(unsigned int i = 0; i < lvpa->HeaderCount(); ++i)
            {
                if(lvpa->GetFileInfo(i).flags & LVPAFLAG_SCRAMBLED)
                {
                    *ldr = new VFSLoaderLVPA(lvpa);
                    break;
                }
            }
        }
        return vd;
    }

    vd->ref--;
    return NULL;
}

VFS_NAMESPACE_END
