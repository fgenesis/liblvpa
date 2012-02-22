#ifndef VFSFILE_LVPA_H
#define VFSFILE_LVPA_H

#include "VFSFile.h"
#include "LVPACompileConfig.h"

LVPA_NAMESPACE_START
class LVPAFile;
LVPA_NAMESPACE_END

VFS_NAMESPACE_START

class VFSFileLVPA : public VFSFile
{
public:
    VFSFileLVPA(LVPA_NAMESPACE_IMPL LVPAFile *src, unsigned int headerId);
    virtual ~VFSFileLVPA();
    virtual bool open(const char *mode = NULL);
    virtual bool isopen(void) const;
    virtual bool iseof(void) const;
    virtual bool close(void);
    virtual bool seek(vfspos pos);
    virtual bool flush(void);
    virtual vfspos getpos(void) const;
    virtual unsigned int read(void *dst, unsigned int bytes);
    virtual unsigned int write(const void *src, unsigned int bytes);
    virtual vfspos size(void);
    virtual vfspos size(vfspos newsize);
    virtual const void *getBuf(allocator_func alloc = NULL, delete_func del = NULL);
    virtual void dropBuf(bool del);
    virtual const char *getType(void) const { return "LVPA"; }

    inline LVPA_NAMESPACE_IMPL LVPAFile *getLVPA(void) const { return _lvpa; }

protected:
    unsigned int _pos;
    unsigned int _size;
    unsigned int _headerId;
    std::string _mode;
    LVPA_NAMESPACE_IMPL LVPAFile *_lvpa;
    char *_fixedStr; // for \n fixed string in text mode. cleared when mode is changed
};

VFS_NAMESPACE_END

#endif
