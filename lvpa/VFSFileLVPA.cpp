#include "LVPACommon.h"
#include "LVPAFile.h"
#include "VFSFileLVPA.h"

VFSFileLVPA::VFSFileLVPA(LVPAFile *src, uint32 headerId) : VFSFile(),
_fixedStr(NULL)
{
    _mode = "b"; // binary mode by default
    _lvpa = src;
    _pos = 0;
    const LVPAFileHeader& hdr = src->GetFileInfo(headerId);
    _size = hdr.realSize;
    _headerId = headerId;

    size_t slashpos = hdr.filename.find_last_of('/');
    _name = slashpos != std::string::npos ? hdr.filename.c_str() + slashpos + 1 : hdr.filename;
    _fullname = hdr.filename;
}

VFSFileLVPA::~VFSFileLVPA()
{
    if(_fixedStr)
        delete [] _fixedStr;
}

bool VFSFileLVPA::open(const char *fn /* = NULL */, char *mode /* = NULL */)
{
    _pos = 0;
    if(mode)
    {
        if(_fixedStr && mode != _mode)
        {
            delete [] _fixedStr;
            _fixedStr = NULL;
        }

        _mode = mode;
    }
    return true; // does not have to be opened
}

bool VFSFileLVPA::isopen(void)
{
    return true; // is always open
}

bool VFSFileLVPA::iseof(void)
{
    return _pos >= _size;
}

const char *VFSFileLVPA::name(void)
{
    return _name.c_str();
}

const char *VFSFileLVPA::fullname(void)
{
    return _fullname.c_str();
}

bool VFSFileLVPA::close(void)
{
    return true; // always open, so this doesn't matter
}

bool VFSFileLVPA::seek(uint64 pos)
{
    if(pos > 0xFFFFFFFF) // LVPA files have uint32 range only
        return false;

    _pos = (uint32)pos;
    return true;
}

bool VFSFileLVPA::flush(void)
{
    return true;
}

uint64 VFSFileLVPA::getpos(void)
{
    return _pos;
}

uint32 VFSFileLVPA::read(char *dst, uint32 bytes)
{
    memblock data = _lvpa->Get(_headerId);
    uint8 *startptr = data.ptr + _pos;
    uint8 *endptr = data.ptr + data.size;
    bytes = std::min(uint32(endptr - startptr), bytes); // limit in case reading over buffer size
    if(_mode.find('b') == std::string::npos)
        strnNLcpy(dst, (const char*)startptr, bytes); // non-binary == text mode
    else
        memcpy(dst, startptr, bytes); //  binary copy
    _pos += bytes;
    return bytes;
}

uint32 VFSFileLVPA::write(char *src, uint32 bytes)
{
    if(getpos() + bytes >= size())
        size(getpos() + bytes); // enlarge if necessary

    memblock data = _lvpa->Get(_headerId);
    memcpy(data.ptr + getpos(), src, bytes);

    return bytes;
}

uint64 VFSFileLVPA::size(void)
{
    return _size;
}

uint64 VFSFileLVPA::size(uint64 newsize)
{
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

const uint8 *VFSFileLVPA::getBuf(void)
{
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
