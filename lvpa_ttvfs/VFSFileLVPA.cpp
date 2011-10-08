#include "LVPAFile.h"
#include "VFSFileLVPA.h"
#include "VFSInternal.h"
#include "VFSTools.h"

VFS_NAMESPACE_START

#ifdef LVPA_NAMESPACE
  using namespace LVPA_NAMESPACE;
#endif

VFSFileLVPA::VFSFileLVPA(LVPAFile *src, unsigned int headerId) : VFSFile(),
_fixedStr(NULL)
{
    _mode = "b"; // binary mode by default
    _lvpa = src;
    _pos = 0;
    const LVPAFileHeader& hdr = src->GetFileInfo(headerId);
    _size = hdr.realSize;
    _headerId = headerId;

    _fullname = FixPath(hdr.filename);
    _name = PathToFileName(_fullname.c_str());
}

VFSFileLVPA::~VFSFileLVPA()
{
    if(_fixedStr)
        delete [] _fixedStr;
}

bool VFSFileLVPA::open(const char *fn /* = NULL */, const char *mode /* = NULL */)
{
    VFS_GUARD_OPT(this);

    // TODO: allow opening a different file than provided in the ctor

    _pos = 0;
    if(mode)
    {
        if(_fixedStr && _mode != mode)
        {
            delete [] _fixedStr;
            _fixedStr = NULL;
        }

        _mode = mode;
    }
    return true; // does not have to be opened
}

bool VFSFileLVPA::isopen(void) const
{
    return true; // is always open
}

bool VFSFileLVPA::iseof(void) const
{
    VFS_GUARD_OPT(this);
    return _pos >= _size;
}

const char *VFSFileLVPA::name(void) const
{
    VFS_GUARD_OPT(this);
    return _name;
}

const char *VFSFileLVPA::fullname(void) const
{
    VFS_GUARD_OPT(this);
    return _fullname.c_str();
}

bool VFSFileLVPA::close(void)
{
    return true; // always open, so this doesn't matter
}

bool VFSFileLVPA::seek(vfspos pos)
{
    if(pos >= 0xFFFFFFFF) // LVPA files have uint32 range only
        return false;

    VFS_GUARD_OPT(this);
    _pos = (uint32)pos;
    return true;
}

bool VFSFileLVPA::flush(void)
{
    return true;
}

vfspos VFSFileLVPA::getpos(void) const
{
    VFS_GUARD_OPT(this);
    return _pos;
}

unsigned int VFSFileLVPA::read(void *dst, unsigned int bytes)
{
    VFS_GUARD_OPT(this);
    memblock data = _lvpa->Get(_headerId);
    uint8 *startptr = data.ptr + _pos;
    uint8 *endptr = data.ptr + data.size;
    bytes = std::min((unsigned int)(endptr - startptr), bytes); // limit in case reading over buffer size
    if(_mode.find('b') == std::string::npos)
        strnNLcpy((char*)dst, (const char*)startptr, bytes); // non-binary == text mode
    else
        memcpy(dst, startptr, bytes); //  binary copy
    _pos += bytes;
    return bytes;
}

unsigned int VFSFileLVPA::write(const void *src, unsigned int bytes)
{
    VFS_GUARD_OPT(this);
    if(getpos() + bytes >= size())
        size(getpos() + bytes); // enlarge if necessary

    memblock data = _lvpa->Get(_headerId);
    memcpy(data.ptr + getpos(), src, bytes);

    return bytes;
}

vfspos VFSFileLVPA::size(void)
{
    VFS_GUARD_OPT(this);
    return _size;
}

vfspos VFSFileLVPA::size(vfspos newsize)
{
    VFS_GUARD_OPT(this);
    if(newsize == size())
        return newsize;

    memblock data = _lvpa->Get(_headerId);
    const LVPAFileHeader& hdr = _lvpa->GetFileInfo(_headerId);
    uint32 n = uint32(newsize);

    const char *solidBlockName = NULL;
    if(hdr.flags & LVPAFLAG_SOLID)
    {
        const LVPAFileHeader& solidHdr = _lvpa->GetFileInfo(hdr.blockId);
        solidBlockName = solidHdr.filename.c_str();
    }
    if(n < data.size)
    {
        data.size = n;
        _lvpa->Add(hdr.filename.c_str(), data, solidBlockName, hdr.algo, hdr.level); // overwrite old entry
    }
    else
    {
        memblock mb(new uint8[n + 4], n); // allocate new, with few extra bytes
        memcpy(mb.ptr, data.ptr, data.size); // copy old
        memset(mb.ptr + data.size, 0, n - data.size + 4); // zero out remaining (with extra bytes)
        _lvpa->Add(hdr.filename.c_str(), mb, solidBlockName, hdr.algo, hdr.level); // overwrite old entry
    }
    return n;
}

const void *VFSFileLVPA::getBuf(void)
{
    VFS_GUARD_OPT(this);
    // _fixedStr gets deleted on mode change, so doing this check here is fine
    if(_fixedStr)
        return (const uint8*)_fixedStr;

    memblock mb = _lvpa->Get(_headerId);
    const uint8 *buf = mb.ptr;
    if(buf && _mode.find("b") == std::string::npos) // text mode?
    {
        _fixedStr = new char[mb.size + 4];
        strnNLcpy(_fixedStr, (const char*)buf);
        buf = (const uint8*)_fixedStr;
    }
    return buf;
}

void VFSFileLVPA::dropBuf(bool del)
{
    VFS_GUARD_OPT(this);
    if(del)
    {
        if(_fixedStr)
        {
            delete [] _fixedStr;
            _fixedStr = NULL;
        }
        _lvpa->Free(_headerId);
    }
    else
    {
        _fixedStr = NULL;
        _lvpa->Drop(_headerId);
    }
}

VFS_NAMESPACE_END
