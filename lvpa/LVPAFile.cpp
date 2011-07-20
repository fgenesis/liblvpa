#include "LVPAInternal.h"
#include "LVPAFile.h"

#include <memory>
#include <set>

#include "MersenneTwister.h"
#include "MyCrc32.h"
#include "LVPAStreamCipher.h"
#include "SHA256Hash.h"
#include "ProgressBar.h"

#include "ICompressor.h"

#ifdef LVPA_SUPPORT_LZMA
#  include "LZMACompressor.h"
#endif
#ifdef LVPA_SUPPORT_LZO
#  include "LZOCompressor.h"
#endif
#ifdef LVPA_SUPPORT_ZLIB
#  include "DeflateCompressor.h"
#endif
#ifdef LVPA_SUPPORT_LZF
#  include "LZFCompressor.h"
#endif

LVPA_NAMESPACE_START

// not the best way...
static ProgressBar *gProgress = NULL;

// these are part of the header of each file
static const char* gMagic = LVPA_MAGIC;
static const uint32 gVersion = LVPA_VERSION;


static int drawCompressProgressBar(void *, uint64 in, uint64 out)
{
    if(gProgress)
    {
        gProgress->done = uint32(in) / 1024; // show in kB
        gProgress->Update();
    }
    return 0; // SZ_OK
}

static ICompressor *allocCompressor(uint8 algo)
{
    switch(algo)
    {
        case LVPAPACK_INHERIT: // we choose the first available one as default
                               // the rest is roughly sorted by compression speed

#ifdef LVPA_SUPPORT_LZF
        case LVPAPACK_LZF:
            return new LZFCompressor;
#endif
#ifdef LVPA_SUPPORT_LZO
        case LVPAPACK_LZO1X:
            return new LZOCompressor;
#endif
#ifdef LVPA_SUPPORT_ZLIB
        case LVPAPACK_DEFLATE:
            return new DeflateCompressor;
#endif
#ifdef LVPA_SUPPORT_LZMA
        case LVPAPACK_LZMA:
            return new LZMACompressor;
#endif
        case LVPAPACK_NONE:
            return new ICompressor; // does nothing
    }

    if(algo < LVPAPACK_MAX_SUPPORTED)
    {
        DEBUG(logerror("allocCompressor(%u): unsupported algorithm id, defaulting to none", uint32(algo)));
        return new ICompressor;
    }
    // rest unknown
    return NULL;
}


ByteBuffer &operator >> (ByteBuffer& bb, LVPAMasterHeader& hdr)
{
    bb >> hdr.version;
    bb >> hdr.flags;
    bb >> hdr.hdrEntries;
    bb >> hdr.packedHdrSize;
    bb >> hdr.realHdrSize;
    bb >> hdr.hdrOffset;
    bb >> hdr.hdrCrcPacked;
    bb >> hdr.hdrCrcReal;
    bb >> hdr.algo;
    bb >> hdr.dataOffs;
    return bb;
}

ByteBuffer &operator << (ByteBuffer& bb, LVPAMasterHeader& hdr)
{
    bb << hdr.version;
    bb << hdr.flags;
    bb << hdr.hdrEntries;
    bb << hdr.packedHdrSize;
    bb << hdr.realHdrSize;
    bb << hdr.hdrOffset;
    bb << hdr.hdrCrcPacked;
    bb << hdr.hdrCrcReal;
    bb << hdr.algo;
    bb << hdr.dataOffs;
    return bb;
}

ByteBuffer &operator >> (ByteBuffer& bb, LVPAFileHeader& h)
{
    bb >> h.flags;
    bb >> h.realSize;
    bb >> h.crcReal;

    if(h.flags & LVPAFLAG_SCRAMBLED)
        bb.read(h.hash, LVPAHash_Size);
    else
        bb >> h.filename;
    
    if(h.flags & LVPAFLAG_PACKED)
    {
        bb >> h.packedSize;
        bb >> h.crcPacked;
        bb >> h.algo;
        bb >> h.level;
    }
    else
    {
        h.packedSize = h.realSize; // this is important, because it will read packedSize bytes from the file
        h.crcPacked = 0;
        h.algo = LVPAPACK_NONE;
        h.level = LVPACOMP_NONE;
    }

    if(h.flags & LVPAFLAG_SOLID)
    {
        bb >> h.blockId;
    }
    else
    {
        h.blockId = 0;
    }

    if(h.flags & (LVPAFLAG_ENCRYPTED | LVPAFLAG_SCRAMBLED))
    {
        bb >> h.cipherWarmup;
    }
    else
    {
        h.cipherWarmup = 0;
    }

    return bb;
}

ByteBuffer &operator << (ByteBuffer& bb, LVPAFileHeader& h)
{
    bb << h.flags;
    bb << h.realSize;
    bb << h.crcReal;

    if(h.flags & LVPAFLAG_SCRAMBLED)
        bb.append(h.hash, LVPAHash_Size);
    else
        bb << h.filename;

    if(h.flags & LVPAFLAG_PACKED)
    {
        bb << h.packedSize;
        bb << h.crcPacked;
        bb << h.algo;
        bb << h.level;
    }

    if(h.flags & LVPAFLAG_SOLID)
    {
        bb << h.blockId;
    }

    if(h.flags & (LVPAFLAG_ENCRYPTED | LVPAFLAG_SCRAMBLED))
        bb << h.cipherWarmup;

    return bb;
}


LVPAFile::LVPAFile()
: _handle(NULL), _realSize(0), _packedSize(0)
{
    _mtrand = new MTRand;
}

LVPAFile::~LVPAFile()
{
    delete _mtrand;
    Clear();
    _CloseFile();
}

void LVPAFile::Clear(bool del /* = true */)
{
    for(uint32 i = 0; i < _headers.size(); ++i)
    {
        // never try to delete files that are part of a bigger allocated block
        if(_headers[i].data.ptr && !_headers[i].otherMem)
        {
            // we can always delete solid blocks, because memory for these is allocated only when loaded
            // from file, and it can never be constant
            if(del || (_headers[i].flags & LVPAFLAG_SOLIDBLOCK))
            {
                delete [] _headers[i].data.ptr;
                _headers[i].data.ptr = NULL;
                _headers[i].data.size = 0;
            }
        }
    }
    _headers.clear();
    _indexes.clear();
}

bool LVPAFile::_OpenFile(void)
{
    if(!_handle)
        _handle = fopen(_ownName.c_str(), "rb");

    return _handle != NULL;
}

void LVPAFile::_CloseFile(void)
{
    if(_handle)
    {
        fclose((FILE*)_handle);
        _handle = NULL;
    }
}

uint32 LVPAFile::GetId(const char *fn)
{
    uint32 id = -1;
    _FindHeaderByName(fn, &id);
    return id;
}

void LVPAFile::_MakeSolid(LVPAFileHeader& h, const char *solidBlockName)
{
    if(!solidBlockName)
        return;
    if(h.flags & LVPAFLAG_SOLIDBLOCK)
    {
        DEBUG(logerror("_MakeSolid: Can't make solid block a solid file!"));
    }
    std::string n(solidBlockName);
    n += '*';

    h.flags |= LVPAFLAG_SOLID;
    if(h.flags & LVPAFLAG_SCRAMBLED)
    {
        DEBUG(logerror("_MakeSolid: Solid file '%s' has SCRAMBLED flag, removing that", h.filename.c_str()));
    }
    h.flags &= ~LVPAFLAG_SCRAMBLED; // can't have that flag in this mode, because can't encrypt based on filename inside solid block

    // this may enlarge _headers vector, thus forcing reallocation and maybe making our h reference invalid
    // but since this method is only called from Add(), and there we take care of reserving enough space, this is ok.
    if(!_FindHeaderByName(n.c_str(), &h.blockId)) // if successful, it will write properly into h.blockId
    {
        h.blockId = SetSolidBlock(solidBlockName); // create not existing with default properties
    }

    // if at least one file inside the solid block must be encrypted, encrypt the whole solid block
    _headers[h.blockId].flags |= (h.flags & LVPAFLAG_ENCRYPTED);
}

uint32 LVPAFile::SetSolidBlock(const char *name, uint8 compression /* = LVPACOMP_INHERIT */, uint8 algo /* = LVPAPACK_INHERIT */)
{
    std::string n(name);
    n += '*';
    uint32 id;
    if(_FindHeaderByName(n.c_str(), &id))
    {
        _headers[id].algo = algo;
        _headers[id].level = compression;
    }
    else // add new solid block
    {
        LVPAFileHeader hdr;
        hdr.flags = LVPAFLAG_SOLIDBLOCK;
        hdr.filename = n;
        hdr.algo = algo;
        hdr.level = compression;
        id = hdr.id = _headers.size();
        _headers.push_back(hdr);
        _indexes[n] = id; // save the index of the hdr we just added
    }
    return id;
}

void LVPAFile::Add(const char *fn, memblock mb, const char *solidBlockName /* = NULL */,
                   uint8 algo /* = LVPAPACK_INHERIT */, uint8 level /* = LVPACOMP_INHERIT */,
                   uint8 encrypt /* = LVPAENCR_INHERIT */, bool scramble /* = false */)
{
    uint32 id;
    if(_FindHeaderByName(fn, &id))
    {
        // already exists, overwrite old with new info
        LVPAFileHeader& hdrRef = _headers[id];
        if(hdrRef.data.ptr && hdrRef.data.ptr != mb.ptr)
        {
            delete [] hdrRef.data.ptr;
            hdrRef.otherMem = false;
            // will be overwritten anyways, not necessary here to set to null values
        }
    }
    else
    {
        // create new header entry
        LVPAFileHeader hdr;
        // save the index it will have after adding
        hdr.id = id = _headers.size();
        _indexes[fn] = id;
        _headers.push_back(hdr);
    }

    // Always reserve a little more space, it would be bad if the vector had to reallocate
    // while we are holding references to its elements.
    // That can happen if a new solid block header entry has to be added in _MakeSolid(), and the vector is out of space.
    _headers.reserve(_headers.size() + 2);

    LVPAFileHeader& h = _headers[id];

    h.filename = fn;
    h.data = mb;
    h.flags = scramble ? LVPAFLAG_SCRAMBLED : LVPAFLAG_NONE;
    h.encryption = encrypt;
    h.algo = algo;
    h.level = level;
    _MakeSolid(h, solidBlockName); // this will also fix up flags a bit if necessary
}

memblock LVPAFile::Remove(const char  *fn)
{
    uint32 id;
    memblock mb;
    if(_FindHeaderByName(fn, &id))
        mb = _headers[id].data; // copy ptr

    _headers[id].data = memblock(); // overwrite with empty
    _indexes.erase(fn); // remove entry

    return mb;
}

bool LVPAFile::Delete(const char *fn)
{
    uint32 id;
    if(_FindHeaderByName(fn, &id))
    {
        memblock mb = _headers[id].data;
        _headers[id].data = memblock(); // overwrite with empty
        _indexes.erase(fn); // remove entry

        if(mb.ptr && !_headers[id].otherMem)
        {
            delete [] mb.ptr;
            return true;
        }
    }
    return false;
}

memblock LVPAFile::Get(const char *fn, bool checkCRC /* = true */)
{
    uint32 id;
    memblock mb;
    if(_FindHeaderByName(fn, &id))
        mb = _PrepareFile(_headers[id], checkCRC);
    return mb;
}

memblock LVPAFile::Get(uint32 index, bool checkCRC /* = true */)
{
    return _PrepareFile(_headers[index], checkCRC);
}

bool LVPAFile::Free(const char *fn)
{
    uint32 id;
    if(_FindHeaderByName(fn, &id))
        return Free(id);
    return true; // file never existed, no problem
}

bool LVPAFile::Free(uint32 id)
{
    LVPAFileHeader& hdrRef = _headers[id];
    hdrRef.sparePtr = NULL; // now its definitely not used anymore.
    if(hdrRef.data.ptr && !hdrRef.otherMem)
    {
        delete [] hdrRef.data.ptr;
        hdrRef.data.ptr = NULL;
        hdrRef.data.size = 0;
        return true;
    }
    return false;
}

uint32 LVPAFile::FreeUnused(void)
{
    uint32 freed = 0;
    std::set<uint8*> ptrsInUse;
    // first, collect all pointers from files inside a solid block
    for(uint32 i = 0; i < HeaderCount(); ++i)
    {
        LVPAFileHeader& h = _headers[i];
        if(h.flags & LVPAFLAG_SOLID)
        {
            // inserting NULL is not a problem
            ptrsInUse.insert(_headers[i].data.ptr);
            ptrsInUse.insert(_headers[i].sparePtr);
        }
    }
    std::set<uint8*>::iterator it;
    for(uint32 i = 0; i < HeaderCount(); ++i)
    {
        LVPAFileHeader& h = _headers[i];
        if(h.flags & LVPAFLAG_SOLIDBLOCK && h.data.ptr) // because we check h.data.ptr here, *it below can be NULL without causing trouble
        {
            bool used = false;
            for(it = ptrsInUse.begin(); it != ptrsInUse.end(); ++it)
            {
                uint8 *ptr = *it;
                if(ptr >= h.data.ptr && ptr < h.data.ptr + h.data.size) // pointer inside solid block memory?
                {
                    used = true;
                    break;
                }
            }
            if(!used)
                freed += Free(h.id);
        }
    }
    return freed;
}


bool LVPAFile::Drop(const char *fn)
{
    uint32 id;
    if(_FindHeaderByName(fn, &id))
        return Drop(id);
    return true; // the file isn't there, no problem.
}

bool LVPAFile::Drop(uint32 id)
{
    LVPAFileHeader& hdrRef = _headers[id];
    if(hdrRef.data.ptr)
        hdrRef.sparePtr = hdrRef.data.ptr;

    hdrRef.data.ptr = NULL;
    hdrRef.data.size = 0;

    return !hdrRef.otherMem;
}

bool LVPAFile::LoadFrom(const char *fn, LVPALoadFlags loadFlags /* = LVPALOAD_NONE */)
{
    _ownName = fn;

    if(!_OpenFile())
        return false;

    Clear();

    uint32 bytes;
    char magic[4];

    bytes = fread(magic, 1, 4, (FILE*)_handle);
    if(bytes != 4 || memcmp(magic, gMagic, 4))
    {
        _CloseFile();
        return false;
    }

    ByteBuffer masterBuf;
    LVPAMasterHeader masterHdr;

    masterBuf.resize(sizeof(LVPAMasterHeader));
    bytes = fread((void*)masterBuf.contents(), 1, sizeof(LVPAMasterHeader), (FILE*)_handle);
    masterBuf >> masterHdr; // not reading it directly via fread() is intentional

    DEBUG(logdebug("master: version: %u", masterHdr.version));
    DEBUG(logdebug("master: flags: %u", masterHdr.flags));

    if(masterHdr.version != gVersion)
    {
        logerror("Unsupported LVPA file version: %u", masterHdr.version);
        _CloseFile();
        return false;
    }

    if(masterHdr.flags & LVPAHDR_ENCRYPTED && !_masterKey.size())
    {
        logerror("Headers are encrypted, but no key set, can't read!");
        _CloseFile();
        return false;
    }

    // decrypt if required
    LVPACipher hdrCiph;
    if(masterHdr.flags & LVPAHDR_ENCRYPTED)
    {
        if(_masterKey.size())
            hdrCiph.Init(&_masterKey[0], _masterKey.size());
        hdrCiph.WarmUp(LVPA_HDR_CIPHER_WARMUP);
        uint32 ox = offsetof(LVPAMasterHeader, packedHdrSize);

        // dumb: due to padding, masterBuf is probably a bit too large, so correct size
        masterBuf.resize(1);
        masterBuf.wpos(0);
        masterBuf.rpos(0);
        masterBuf << masterHdr;

        hdrCiph.Apply((uint8*)masterBuf.contents() + ox, masterBuf.size() - ox);
        masterBuf.rpos(0);
        masterBuf >> masterHdr; // read again
    }

    DEBUG(logdebug("master: data offset: %u", masterHdr.dataOffs));
    DEBUG(logdebug("master: header offset: %u", masterHdr.hdrOffset));
    DEBUG(logdebug("master: header entries: %u", masterHdr.hdrEntries));
    DEBUG(logdebug("master: packed header crc: %X", masterHdr.hdrCrcPacked));
    DEBUG(logdebug("master: unpacked header crc: %X", masterHdr.hdrCrcReal))
    DEBUG(logdebug("master: packed size: %u", masterHdr.packedHdrSize));
    DEBUG(logdebug("master: real size: %u", masterHdr.realHdrSize));

    // sanity check
    if( !(masterHdr.hdrEntries && masterHdr.packedHdrSize && masterHdr.realHdrSize) )
    {
        logerror("Can't read headers, file contains no valid data");
        _CloseFile();
        return false;
    }

    // ... space for additional data/headers here...

    // seek to the file header's offset if we are not yet there
    if(ftell((FILE*)_handle) != masterHdr.hdrOffset)
        fseek((FILE*)_handle, masterHdr.hdrOffset, SEEK_SET);

    std::auto_ptr<ICompressor> hdrBuf(allocCompressor(masterHdr.algo));

    if(!hdrBuf.get())
    {
        logerror("Unable to decompress headers, wrong encryption key?");
        _CloseFile();
        return false;
    }


    // read the (packed) file headers
    hdrBuf->resize(masterHdr.packedHdrSize);
    bytes = fread((void*)hdrBuf->contents(), 1, masterHdr.packedHdrSize, (FILE*)_handle);
    if(bytes != masterHdr.packedHdrSize)
    {
        logerror("Can't read headers, file is corrupt");
        _CloseFile();
        return false;
    }

    // decrypt if necessary
    if(masterHdr.flags & LVPAHDR_ENCRYPTED)
    {
        hdrCiph.Apply((uint8*)hdrBuf->contents(), hdrBuf->size());
    }
    
    // decompress the headers if packed
    if(masterHdr.flags & LVPAHDR_PACKED)
    {
        // check CRC of packed header data
        if(CRC32::Calc((uint8*)hdrBuf->contents(), hdrBuf->size()) != masterHdr.hdrCrcPacked)
        {
            logerror("CRC mismatch, packed header is damaged");
            _CloseFile();
            return false;
        }

        hdrBuf->Compressed(true); // tell the buf that it is compressed so it will allow decompression
        hdrBuf->RealSize(masterHdr.realHdrSize);
        hdrBuf->Decompress();
    }

    // check CRC of unpacked header data
    if(CRC32::Calc((uint8*)hdrBuf->contents(), hdrBuf->size()) != masterHdr.hdrCrcReal)
    {
        logerror("CRC mismatch, unpacked header is damaged");
        _CloseFile();
        return false;
    }

    _realSize = _packedSize = 0;

    // read the headers
    _headers.resize(masterHdr.hdrEntries);
    uint32 dataStartOffs = masterHdr.dataOffs;
    uint32 solidOffs = 0;
    for(uint32 i = 0; i < masterHdr.hdrEntries; ++i)
    {
        LVPAFileHeader &h = _headers[i];
        *hdrBuf >> h;
        h.good = true;

        DEBUG(logdebug("'%s' bytes: %u; blockId: %u; [%s%s%s%s%s]",
            h.filename.c_str(), h.packedSize, h.blockId,
            (h.flags & LVPAFLAG_PACKED) ? "PACKED " : "",
            (h.flags & LVPAFLAG_SOLID) ? "SOLID " : "",
            (h.flags & LVPAFLAG_SOLIDBLOCK) ? "SBLOCK " : "",
            (h.flags & LVPAFLAG_ENCRYPTED) ? "ENCR " : "",
            (h.flags & LVPAFLAG_SCRAMBLED) ? "SCRAM " : ""
            ));

        // sanity check - can't be in a solid block and a solid block itself
        if((h.flags & LVPAFLAG_SOLID) &&( h.flags & LVPAFLAG_SOLIDBLOCK))
        {
            h.good = false;
            logerror("File '%s' has wrong/incompatible flags, whoops!", h.filename.c_str());
            _CloseFile();
            return false;
        }

        // for stats -- do not account files inside a solid block, because the solid block is likely packed, not the individual files
        if(!(h.flags & LVPAFLAG_SOLID))
            _packedSize += h.packedSize;
        // -- here, do not account solid blocks, because the individual files' real size matters
        if(!(h.flags & LVPAFLAG_SOLIDBLOCK))
            _realSize += h.realSize;
    }

    // at this point we have processed all headers
    _CreateIndexes();
    _CalcOffsets(masterHdr.dataOffs);

    // iterate over all files if requested
    if(loadFlags & LVPALOAD_SOLID)
    {
        for(uint32 i = 0; i < masterHdr.hdrEntries; ++i)
            if(_headers[i].flags & LVPAFLAG_SOLID || loadFlags & LVPALOAD_ALL)
                if(!(_headers[i].flags & LVPAFLAG_SCRAMBLED))
                    _PrepareFile(_headers[i], true);

        if(loadFlags & LVPALOAD_ALL)
            _CloseFile(); // got everything, file can be closed
    }
    // leave the file open, as we may want to read more later on

    return true;
}

bool LVPAFile::Save(uint8 compression, uint8 algo /* = LVPAPACK_INHERIT */, bool encrypt /* = false */)
{
    return SaveAs(_ownName.c_str(), compression, algo, encrypt);
}

// note: this function must NOT modify existing headers in memory!
bool LVPAFile::SaveAs(const char *fn, uint8 compression /* = LVPA_DEFAULT_LEVEL */, uint8 algo /* = LVPAPACK_INHERIT */,
                      bool encrypt /* = false */)
{
    // check before showing progress bar
    if(!_headers.size())
    {
        logerror("LVPA: No files to write to '%s'", fn);
        return false;
    }

    // apply default settings for INHERIT modes
    if(compression == LVPACOMP_INHERIT)
        compression = LVPA_DEFAULT_LEVEL;
    if(algo == LVPAPACK_INHERIT)
        algo = LVPAPACK_LZMA;

    std::auto_ptr<ICompressor> zhdr(allocCompressor(algo));
    if(!zhdr.get())
    {
        logerror("LVPA: Unknown compression method '%u'", (uint32)algo);
        return false;
    }

    if(encrypt && _masterKey.empty())
    {
        logerror("WARNING: LVPAFile: File should be encrypted, but no master key - not encrypting.");
        encrypt = false;
    }

    zhdr->reserve(_headers.size() * (sizeof(LVPAFileHeader) + 30)); // guess size

    _realSize = _packedSize = 0;

    // we copy all headers, because some have to be modified while data are packed, these changes would likely mess up when reading data
    std::vector<LVPAFileHeader> headersCopy = _headers;

    // compressing is possibly going to take some time, better to show a progress bar
    ProgressBar bar;
    gProgress = &bar;
    bar.msg = "Preparing:    ";

    // find out sizes early to prevent re-allocation,
    // and prepare some of the header fields
    for(uint32 i = 0; i < headersCopy.size(); ++i)
    {
        LVPAFileHeader& h = headersCopy[i];

        if(!h.good)
        {
            logerror("Damaged file: '%s'", h.filename.c_str());
            continue;
        }

        h.crcPacked = h.crcReal = 0;

        // make used algo/compression level consistent
        // level 0 is always no algorithm, and vice versa
        if(h.algo == LVPAPACK_NONE)
            h.level = 0;
        else if(h.level == LVPACOMP_NONE)
            h.algo = 0;

        // overwrite settings if not specified otherwise
        // for now, only solid blocks, or non-solid files, the remaining solid files will come in next iteration
        if((h.flags & LVPAFLAG_SOLIDBLOCK) || !(h.flags & LVPAFLAG_SOLID))
        {
            if(h.flags & LVPAFLAG_SOLIDBLOCK)
                h.realSize = h.packedSize = 0; // we will fill this soon
            else
            {
                h.realSize = h.packedSize = h.data.size;
                _realSize += h.realSize; // for stats
            }

            h.blockId = 0;
            DEBUG(h.blockId = -1); // to see if an error occurs - this value should never be used with these flags
            
            if(h.level == LVPACOMP_INHERIT)
                h.level = compression;
            if(h.algo == LVPAPACK_INHERIT)
                h.algo = algo;

            if(h.encryption == LVPAENCR_INHERIT)
            {
                if(encrypt)
                    h.flags |= LVPAFLAG_ENCRYPTED;
                else
                    h.flags &= ~LVPAFLAG_ENCRYPTED;
            }
            else if(h.encryption == LVPAENCR_NONE)
                h.flags &= ~LVPAFLAG_ENCRYPTED;
            else
                h.flags |= LVPAFLAG_ENCRYPTED;
        }   
    }

    // one buf for each file - not all have to be used.
    // it WILL be used if a file has LVPAFLAG_PACKED set, which means the data went into a compressor
    // they might not be packed if the data are incompressible, in this case, the original data in memory are used
    AutoPtrVector<ICompressor> fileBufs(headersCopy.size()); 

    // first iteration - find out required sizes for the solid block buffers, and renumber solid block ids for the files stored inside
    for(uint32 i = 0; i < headersCopy.size(); ++i)
    {
        LVPAFileHeader& h = headersCopy[i];
        if(!h.good)
            continue;

        // files in a solid block are never marked as packed, because the solid block itself is already packed
        // but because we set the packed flag later, we can remove all of them.
        if(h.flags & LVPAFLAG_SOLID)
        {
            h.realSize = h.packedSize = h.data.size;
            _realSize += h.data.size; // for stats
            h.flags &= ~LVPAFLAG_PACKED;
            headersCopy[h.blockId].realSize += h.realSize + LVPA_EXTRA_BUFSIZE;

            // overwrite settings if not specified otherwise
            // solid blocks were done in last iteration, now adjust the remaining files
            // files in a solid block will inherit its settings
            LVPAFileHeader& sh = headersCopy[h.blockId];
            if(h.level == LVPACOMP_INHERIT)
                h.level = sh.level;
            if(h.algo == LVPAPACK_INHERIT)
                h.algo = sh.algo;

            // inherit from global settings, or just encrypt right away if set to do so
            // in case a file inside a solid block should be encrypted, the whole solid block needs to be encrypted,
            // so adjust its settings if necessary.
            if(h.encryption == LVPAENCR_ENABLED || (encrypt && h.encryption == LVPAENCR_INHERIT))
            {
                h.flags &= ~LVPAFLAG_ENCRYPTED; // remove it from this file, because the solid block gets encrypted, not this file
                sh.flags |= LVPAFLAG_ENCRYPTED;
            }
            else // not encrypting this file, but do not change the solid block's settings, as there may be other encrypted files in it.
            {
                h.flags &= ~LVPAFLAG_ENCRYPTED;
            }
        }
    }

    bar.total = _realSize / 1024; // we know the total size now, show in kB

    // second iteration - allocate the buffers and reserve sizes
    for(uint32 i = 0; i < headersCopy.size(); ++i)
    {
        const LVPAFileHeader& h = headersCopy[i];
        // that indicates we need to write the file to a buffer
        if(h.good && !(h.flags & LVPAFLAG_SOLID) && ((h.flags & LVPAFLAG_SOLIDBLOCK) || h.level != LVPACOMP_NONE))
        {
            // each file (or solid block) can have its own compression algo, and level
            fileBufs.v[i] = allocCompressor(h.algo);
            fileBufs.v[i]->reserve(h.realSize);
        }
    }
    uint8 solidPadding[LVPA_EXTRA_BUFSIZE];
    memset(&solidPadding[0], 0, LVPA_EXTRA_BUFSIZE);
    // third iteration - append solid files to their blocks
    for(uint32 i = 0; i < headersCopy.size(); ++i)
    {
        LVPAFileHeader& h = headersCopy[i];
        if(h.good && (h.flags & LVPAFLAG_SOLID))
        {
            ICompressor *solidblock = fileBufs.v[h.blockId];
            DEBUG(ASSERT(solidblock));
            solidblock->append(h.data.ptr, h.data.size);
            solidblock->append(&solidPadding[0], LVPA_EXTRA_BUFSIZE);
        }
    }

    bar.msg = "Compressing:  ";

    // fourth iteration - append each non-solid file to its buffer, and compress each file / solid block
    // also append each header to the header compressor buf
    uint32 writtenHeaders = 0;
    for(uint32 i = 0; i < headersCopy.size(); ++i)
    {
        LVPAFileHeader& h = headersCopy[i];
        if(!h.good)
            continue;
        ICompressor *block = fileBufs.v[i];
        if(block)
        {
            // solid blocks were already filled, and solid files didn't get their own buf allocated
            if(!(h.flags & (LVPAFLAG_SOLID | LVPAFLAG_SOLIDBLOCK)))
            {
                DEBUG(ASSERT(block->size() == 0));
                block->append(h.data.ptr, h.data.size);
            }

            // calc unpacked crc before compressing
            if(block->size())
                h.crcReal = CRC32::Calc(block->contents(), block->size());
            
            if(h.level != LVPACOMP_NONE)
                block->Compress(h.level, drawCompressProgressBar);

            h.packedSize = block->size();
            if(block->Compressed())
            {
                h.flags |= LVPAFLAG_PACKED; // this flag was cleared earlier
                h.crcPacked = CRC32::Calc(block->contents(), block->size());
            }

            // encrypt? these blocks will be thrown away, so we can just directly apply encryption
            if(block->size())
                _CryptBlock((uint8*)block->contents(), h, true);

            bar.PartialFix();

            // for stats
            _packedSize += block->size();
        }
        else
        {
            // we still need to calc crc
            h.crcReal = CRC32::Calc(h.data.ptr, h.data.size);

            // if the file should be encrypted, we have to make a copy anyways.
            if(h.data.size && (h.flags & (LVPAFLAG_ENCRYPTED | LVPAFLAG_SCRAMBLED)))
            {
                fileBufs.v[i] = new ICompressor;
                fileBufs.v[i]->append(h.data.ptr, h.data.size);
                _CryptBlock((uint8*)fileBufs.v[i]->contents(), h, true);
            }

            // for stats - not encrypted, but it needs to be accounted
            if(!(h.flags & LVPAFLAG_SOLID))
                _packedSize += h.data.size;
        }

        *zhdr << h;
        ++writtenHeaders;
    }

    if(!writtenHeaders)
    {
        logerror("LVPA: No valid files - there were some, but they got lost on the way. Something is wrong.");
        return false;
    }

    // prepare master header (unfinished!)
    LVPAMasterHeader masterHdr;
    ByteBuffer masterBuf;

    masterHdr.version = gVersion;
    masterHdr.hdrEntries = writtenHeaders;
    masterHdr.algo = algo;
    masterHdr.realHdrSize = zhdr->size();
    masterHdr.hdrCrcReal = 0;
    masterHdr.hdrCrcPacked = 0;

    if(zhdr->size())
    {
        masterHdr.hdrCrcReal = CRC32::Calc(zhdr->contents(), zhdr->size());

        // now we can compress the headers
        if(compression)
            zhdr->Compress(compression);

        if(zhdr->Compressed())
            masterHdr.hdrCrcPacked = CRC32::Calc(zhdr->contents(), zhdr->size());
    }

    masterHdr.flags = zhdr->Compressed() ? LVPAHDR_PACKED : LVPAHDR_NONE;
    if(encrypt)
        masterHdr.flags |= LVPAHDR_ENCRYPTED;
    // its not bad if its not packed now, then packed and unpacked sizes are just equal
    masterHdr.packedHdrSize = zhdr->size();

    // we don't know these yet
    masterHdr.hdrOffset = 0;
    masterHdr.dataOffs = 0;

    masterBuf << masterHdr;


    // -- write everything into the container file --
    gProgress = NULL;
    bar.Reset();
    bar.msg = "Writing file: ";
    bar.total = writtenHeaders;
    bar.Update();

    // close the file if already open, to allow overwriting
    _CloseFile();

    FILE *outfile = fopen(fn, "wb");
    if(!outfile)
    {
        logerror("Failed to open '%s' for writing!", fn);
        return false;
    }

    uint32 written = fwrite(gMagic, 1, 4, outfile);
    written += fwrite(masterBuf.contents(), 1, masterBuf.size(), outfile); // this will be overwritten later
    if(written != masterBuf.size() + 4)
    {
        logerror("Failed writing master header to LVPA file - disk full?");
        fclose(outfile);
        return false;
    }

    // ... space for additional data ...

    // now we know all fields of the master header, write it again
    masterHdr.hdrOffset = ftell(outfile);
    masterHdr.dataOffs = masterHdr.hdrOffset + zhdr->size(); // data follows directly after the headers, so we can safely assume this here

    masterBuf.wpos(0); // overwrite
    masterBuf << masterHdr;

    if(encrypt)
    {
        LVPACipher hdrCiph;
        hdrCiph.Init(&_masterKey[0], _masterKey.size()); // size was checked above
        hdrCiph.WarmUp(LVPA_HDR_CIPHER_WARMUP);
        uint32 ox = offsetof(LVPAMasterHeader, packedHdrSize);
        hdrCiph.Apply((uint8*)masterBuf.contents() + ox, masterBuf.size() - ox);
        hdrCiph.Apply((uint8*)zhdr->contents(), zhdr->size());
    }

    // write headers (we are still at the correct position in the file)
    written = fwrite(zhdr->contents(), 1, zhdr->size(), outfile);
    if(written != zhdr->size())
    {
        logerror("Failed writing headers block to LVPA file - disk full?");
        fclose(outfile);
        return false;
    }

    // write fixed master header
    fseek(outfile, 4, SEEK_SET); // after "LVPA"
    fwrite(masterBuf.contents(), 1, masterBuf.size(), outfile);
    fseek(outfile, masterHdr.dataOffs, SEEK_SET); // seek back

    // write the files
    for(uint32 i = 0; i < headersCopy.size(); ++i)
    {
        LVPAFileHeader& h = headersCopy[i];
        if(!h.good || (h.flags & LVPAFLAG_SOLID))
            continue;
        ICompressor *block = fileBufs.v[i];
        uint32 expected;

        if(block && block->size())
        {
            written = fwrite(block->contents(), 1, block->size(), outfile);
            expected = block->size();
        }
        else
        {
            expected = h.data.size;
            written = 0;
            if(h.data.size)
                written = fwrite(h.data.ptr, 1, h.data.size, outfile);
        }
        if(written != expected)
        {
            logerror("Failed writing data to LVPA file - disk full?");
            fclose(outfile);
            return false;
        }
        ++bar.done;
        bar.Update();
    }

    fclose(outfile);
    bar.Finalize();
    return true;
}

memblock LVPAFile::_PrepareFile(LVPAFileHeader& h, bool checkCRC /* = true */)
{
    // h.good is set to false if there was a previous attempt to load the file that failed irrecoverably
    if(!h.good)
        return memblock();

    // already known? -- if h.data.ptr != NULL, the data must have been fully decrypted and unpacked already.
    if(h.data.ptr)
    {
        return h.data;
    }
    else
    {
        if(h.flags & LVPAFLAG_SOLID)
        {
            // these can never appear on files inside solid blocks
            h.flags &= ~(LVPAFLAG_PACKED | LVPAFLAG_ENCRYPTED | LVPAFLAG_SCRAMBLED);

            if(h.blockId >= _headers.size())
            {
                logerror("File '%s' is marked as solid (block %u), but there is no solid block with that ID", h.filename.c_str(), h.blockId);
                h.good = false;
                return memblock();
            }

            memblock solidMem = _PrepareFile(_headers[h.blockId], checkCRC);
            if(!solidMem.ptr)
            {
                logerror("Unable to load solid block for file '%s'", h.filename.c_str());
                return memblock();
            }

            if(h.offset + h.packedSize <= solidMem.size)
            {
                h.data.ptr = solidMem.ptr + h.offset;
                h.data.size = h.realSize;
                h.otherMem = true;
            }
            else
            {
                logerror("Solid file '%s' exceeds solid block length, can't read", h.filename.c_str());
                h.good = false;
                return memblock();
            }
        }
        else
        {
            h.otherMem = false;
            h.data = _UnpackFile(h);
        }

        if(!h.data.ptr) // if its still NULL, it failed to load
        {
            return memblock();
        }
    }

    // optionally check CRC32 of the unpacked data
    // -- for uncompressed files, this is the only chance to find out whether the decryption key was correct
    if(checkCRC && CRC32::Calc(h.data.ptr, h.data.size) != h.crcReal)
    {
        logerror("CRC mismatch for unpacked '%s', file is corrupt, or decrypt fail", h.filename.c_str());
        if(!(h.flags & LVPAFLAG_ENCRYPTED))
            h.good = false; // if its not encrypted, there is nothing that could fix this
        return memblock();
    }

    return h.data;
}

memblock LVPAFile::_UnpackFile(LVPAFileHeader& h)
{
    DEBUG(ASSERT(h.good && !(h.flags & LVPAFLAG_SOLID))); // if this flag is set this function should not be entered

    ICompressor *buf = NULL;
    memblock target;

    if(h.flags & LVPAFLAG_PACKED)
    {
        buf = allocCompressor(h.algo);
        target.size = h.packedSize;
        if(target.size)
        {
            buf->resize(target.size);
            target.ptr = (uint8*)buf->contents();
        }
        buf->wpos(0);
        buf->rpos(0);
    }
    else
    {
        target.size = h.realSize;
        target.ptr = new uint8[target.size + LVPA_EXTRA_BUFSIZE];
    }

    if(!_DecryptFile(target, h))
    {
        if(buf)
            delete buf;
        return memblock();
    }

    // if the file is compressed, we allocated a decompressor buf earlier
    if(buf)
    {
        // check CRC32 of the packed data
        if(CRC32::Calc(target.ptr, target.size) != h.crcPacked)
        {
            logerror("CRC mismatch for packed '%s', file is corrupt, or decrypt fail", h.filename.c_str());
            if(!(h.flags & LVPAFLAG_ENCRYPTED))
                h.good = false; // if its not encrypted, there is nothing that could fix this
            return memblock();
        }

        buf->Compressed(true); // tell the buf that it is compressed so it will allow decompression
        buf->RealSize(h.realSize);
        DEBUG(logdebug("'%s': uncompressing %u -> %u", h.filename.c_str(), h.packedSize, h.realSize));
        buf->Decompress();
        target.size = buf->size();
        target.ptr = new uint8[target.size + LVPA_EXTRA_BUFSIZE];
        
        if(target.size)
            buf->read(target.ptr, target.size);

        memset(target.ptr + target.size, 0, LVPA_EXTRA_BUFSIZE); // zero out extra space

        delete buf;
    }

    return target;
}

bool LVPAFile::_DecryptFile(memblock &target, LVPAFileHeader& h)
{
    DEBUG(ASSERT(h.good && !(h.flags & LVPAFLAG_SOLID))); // if this flag is set this function should not be entered

    if(!_LoadFile(target, h))
        return false;

    if(!_CryptBlock(target.ptr, h, false))
    {
        // failed to decrypt - this means the settings are not sufficient to decrypt the file,
        // NOT that the key was wrong!
        return false;
    }

    return true;
}

bool LVPAFile::_LoadFile(memblock& target, LVPAFileHeader& h)
{
    DEBUG(ASSERT(h.good && !(h.flags & LVPAFLAG_SOLID))); // if this flag is set this function should not be entered

    if(!_OpenFile())
        return false;

    // seek if necessary
    if(ftell((FILE*)_handle) != h.offset)
        fseek((FILE*)_handle, h.offset, SEEK_SET);

    uint32 bytes = fread(target.ptr, 1, target.size, (FILE*)_handle);
    if(bytes != h.packedSize)
    {
        logerror("Unable to read enough data for file '%s'", h.filename.c_str());
        h.good = false;
        return false;
    }
    return true;
}

const LVPAFileHeader& LVPAFile::GetFileInfo(uint32 i) const
{
    DEBUG(ASSERT(i < _headers.size()));
    return _headers[i];
}

void LVPAFile::_CreateIndexes(void)
{
    for(uint32 i = 0; i < _headers.size(); ++i)
    {
        LVPAFileHeader& h = _headers[i];
        h.id = i; // this is probably not necessary...

        // do not index if the file name is not known
        if(h.flags & LVPAFLAG_SCRAMBLED && h.filename.empty())
            continue;

        _indexes[h.filename] = i;
    }
}

void LVPAFile::_CalcOffsets(uint32 startOffset)
{
    std::vector<uint32> solidOffsets(_headers.size());
    std::fill(solidOffsets.begin(), solidOffsets.end(), 0);
    
    for(uint32 i = 0; i < _headers.size(); ++i)
    {
        LVPAFileHeader& h = _headers[i];

        if(h.flags & LVPAFLAG_SOLID) // solid files use relative addressing inside their solid block
        {
            uint32& o = solidOffsets[h.blockId];
            h.offset = o;
            o += h.realSize + LVPA_EXTRA_BUFSIZE;
            DEBUG(logdebug("Rel offset %u for '%s'", h.offset, h.filename.c_str()));
        }
        else // non-solid files or solid blocks themselves use absolute file position addressing
        {
            h.offset = startOffset;
            startOffset += h.packedSize;
            DEBUG(logdebug("Abs offset %u for '%s'", h.offset, h.filename.c_str()));
        }
        
    }
}

void LVPAFile::RandomSeed(uint32 seed)
{
    _mtrand->seed(seed);
}

bool LVPAFile::AllGood(void) const
{
    for(uint32 i = 0; i < _headers.size(); ++i)
    if(!_headers[i].good)
        return false;

    return true;
}

void LVPAFile::SetMasterKey(const uint8 *key, uint32 size)
{
    _masterKey.resize(size);
    if(size)
    {
        memcpy(&_masterKey[0], key, size);
        LVPAHash::Calc(&_masterSalt[0], key, size);
    }
}

void LVPAFile::_CalcSaltedFilenameHash(uint8 *dst, const std::string& fn)
{
    LVPAHash sha(dst);
    sha.Update((uint8*)fn.c_str(), fn.size() + 1); // this DOES include the terminating '\0'
    if(_masterKey.size()) // if so, we also have the salt
        sha.Update(&_masterSalt[0], LVPAHash_Size);
    sha.Finalize();
}

bool LVPAFile::_CryptBlock(uint8 *buf, LVPAFileHeader& hdr, bool writeMode)
{
    if(!(hdr.flags & (LVPAFLAG_ENCRYPTED | LVPAFLAG_SCRAMBLED)))
        return true; // not encrypted, not scrambled, nothing to do, all fine

    uint8 mem[LVPAHash_Size];
    LVPACipher ciph;

    if(hdr.flags & LVPAFLAG_SCRAMBLED)
    {
        if(hdr.filename.empty())
        {
            DEBUG(logerror("File is scrambled, but given filename is empty, can't decrypt"));
            return false;
        }
        _CalcSaltedFilenameHash(&mem[0], hdr.filename.c_str());
        if(writeMode)
        {
            memcpy(&hdr.hash[0], &mem[0], LVPAHash_Size);
        }
        else if(memcmp(&mem[0], hdr.hash, LVPAHash_Size))
        {
            DEBUG(logerror("_CryptBlock: wrong file name"));
            return false;
        }

        LVPAHash::Calc(&mem[0], (uint8*)hdr.filename.c_str(), hdr.filename.length()); // this does NOT include the terminating '\0'

        if(hdr.flags & LVPAFLAG_ENCRYPTED)
        {
            LVPAHash sha(&mem[0]);
            if(_masterKey.size())
                sha.Update(&_masterKey[0], _masterKey.size());
            sha.Update(&mem[0], LVPAHash_Size);
            sha.Finalize();
        }
        
        ciph.Init(&mem[0], LVPAHash_Size);
    }
    else if(hdr.flags & LVPAFLAG_ENCRYPTED)
    {
        if(_masterKey.size())
            ciph.Init(&_masterKey[0], _masterKey.size());
        else
        {
            DEBUG(logerror("_CryptBlock: encrypted, not scrambled, and no master key!"));
            return false;
        }
    }

    if(!hdr.cipherWarmup)
    {
        uint32 r = _mtrand->randInt(128) + 30;
        hdr.cipherWarmup = uint16(r * sizeof(uint32)); // for speed, we always use full uint32 blocks
    }

    ciph.WarmUp(hdr.cipherWarmup);
    ciph.Apply(buf, hdr.packedSize); // packedSize because the file is encrypted AFTER compression!

    // CRC is checked elsewhere

    return true;
}

bool LVPAFile::_FindHeaderByName(const char *fn, uint32 *id)
{
    LVPAIndexMap::iterator it = _indexes.find(fn);
    if(it != _indexes.end())
    {
        *id = it->second;
        return true;
    }

    // not indexed, maybe its scrambled & hashed?
    uint8 mem[LVPAHash_Size];
    _CalcSaltedFilenameHash(&mem[0], fn);

    if(_FindHeaderByHash(&mem[0], id))
    {
        _headers[*id].filename = fn; // index now, for quick lookup later, and for later unscrambling, if needed
        _indexes[fn] = *id;
        return true;
    }

    return false;
}

bool LVPAFile::_FindHeaderByHash(uint8 *hash, uint32 *id)
{
    for(uint32 i = 0; i < _headers.size(); ++i)
    {
        LVPAFileHeader &h = _headers[i];
        if(h.flags & LVPAFLAG_SCRAMBLED)
        {
            if(!memcmp(hash, h.hash, LVPAHash_Size))
            {
                *id = i;
                return true;
            }
        }
    }
    return false;
}


bool IsSupported(LVPAAlgorithms algo)
{
    switch(algo)
    {
#ifdef LVPA_SUPPORT_LZF
    case LVPAPACK_LZF:
        return true;
#endif
#ifdef LVPA_SUPPORT_LZO
    case LVPAPACK_LZO1X:
        return true;
#endif
#ifdef LVPA_SUPPORT_ZLIB
    case LVPAPACK_DEFLATE:
        return true;
#endif
#ifdef LVPA_SUPPORT_LZMA
    case LVPAPACK_LZMA:
        return true;
#endif
    case LVPAPACK_INHERIT:
    case LVPAPACK_NONE:
        return true; // doing nothing is always supported :)
    }
    return false;
}

LVPA_NAMESPACE_END
