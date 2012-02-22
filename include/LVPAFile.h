#ifndef LVPAFILE_H
#define LVPAFILE_H

#include "LVPACommon.h"

#include <map>
#include <vector>
#include <string>

LVPA_NAMESPACE_START

// default compression level used if nothing else is specified [0..9]
#define LVPA_DEFAULT_LEVEL LVPACOMP_NORMAL


// --- Changing any of the settings below will make this library version incompatible with others.
// --- If this is intended, go ahead. If not, stay away from these defines!

// each buffer allocated for files gets this amount of extra padding bytes (for pure text files that need to end with \0, for example)
#define LVPA_EXTRA_BUFSIZE 4

// multiple ciphers would be a bit overkill right now, so we use only this
#define LVPACipher HPRC4LikeCipher
#define LVPAHash SHA256Hash
#define LVPAHash_Size 32

// these are part of the header of each file
#define LVPA_MAGIC "LVPA"
#define LVPA_VERSION 0
#define LVPA_HDR_CIPHER_WARMUP 1337

enum LVPAMasterFlags
{
    LVPAHDR_NONE        = 0x00,
    LVPAHDR_PACKED      = 0x01,
    LVPAHDR_ENCRYPTED   = 0x02,
};

enum LVPAFileFlags
{
    LVPAFLAG_NONE       = 0x00,
    LVPAFLAG_PACKED     = 0x01, // file is packed (LZMA, etc)
    LVPAFLAG_SOLID      = 0x02, // file is in a solid block
    LVPAFLAG_SOLIDBLOCK = 0x04, // file is not an actual file, but a solid block
    LVPAFLAG_ENCRYPTED  = 0x08, // file is encrypted (requires master key to decrypt)
    LVPAFLAG_SCRAMBLED  = 0x10, // file name in header is just a (salted) hash, and the file is encrypted using HASH(filename) as key.
                                // Note: the actual "salt" is HASH(master key), be sure to have one set should you use this,
                                // otherwise it is possible to extract the file without knowing its name by simply using its hash !!
                                // If ENCRYPTED and SCRAMBLED are combined, the key to encrypt the file will be HASH(master key .. HASH(filename))
};

// TODO: Deprecate this!
enum LVPALoadFlags
{
    LVPALOAD_NONE     = 0x00, // load only headers
    LVPALOAD_SOLID    = 0x01, // load all solid blocks and all files within
    LVPALOAD_ALL      = 0xFF  // load all files
};

enum LVPAAlgos
{
    LVPAPACK_NONE,
    LVPAPACK_LZMA,
    LVPAPACK_LZO1X,
    LVPAPACK_DEFLATE,
    LVPAPACK_LZF,
    LVPAPACK_LZHAM,

    LVPAPACK_MAX_SUPPORTED, // must be after last algo

    LVPAPACK_INHERIT = 0xFF // select the one used by parent
};

enum LVPAEncr
{
    LVPAENCR_NONE,
    LVPAENCR_ENABLED, // currently, the code supports only on, off, and inherit

    LVPAENCR_INHERIT = 0xFF // select the one used by parent
};

enum LVPAComprLevels
{
    LVPACOMP_NONE = 0, // just store
    LVPACOMP_FASTEST = 1, // very fast, low memory
    LVPACOMP_FAST = 2,
    LVPACOMP_NORMAL = 3, // quite fast and good compression
    LVPACOMP_GOOD = 5,
    LVPACOMP_BETTER = 7,
    LVPACOMP_ULTRA = 9, // slow, VERY memory intensive

    LVPACOMP_INHERIT = 0xFF // use whatever is used for the headers or parent
};

struct LVPAMasterHeader
{
    // char magic[4]; // "LVPA"
    uint32 version;
    uint32 flags; // see LVPAMasterFlags
    // ---- encryption from here, if enabled ---
    uint32 packedHdrSize;
    uint32 realHdrSize; // unpacked size
    uint32 hdrOffset; // absolute offset where the compressed headers start
    uint32 hdrCrcPacked; // checksum for the packed headers
    uint32 hdrCrcReal; // checksum for the unpacked headers
    uint32 hdrEntries; // amount of LVPAFileHeaders
    uint32 dataOffs; // absolute offset where the data blocks start
    uint8 algo; // algorithm used to compress the headers
    // level is not explicitly stored
};

struct LVPAFileHeader
{
    LVPAFileHeader()
        : packedSize(0), realSize(0), crcPacked(0), crcReal(0), blockId(0), cipherWarmup(0),
          flags(LVPAFLAG_NONE), algo(LVPAPACK_NONE), level(LVPACOMP_NONE),
          id(-1), offset(-1), encryption(LVPAENCR_NONE), good(true), checkedCRC(false), checkedCRCPacked(false),
          otherMem(false), sparePtr(NULL)
    {
        memset(&hash[0], 0, LVPAHash_Size);
    }

    // these are stored in the file
    std::string filename; // empty/unused if LVPAFLAG_SCRAMBLED is set
    uint32 packedSize; // size in bytes in current file (usually packed)
    uint32 realSize; // unpacked size of the file, for array allocation
    uint32 crcPacked; // checksum for the packed data block
    uint32 crcReal; // checksum for the unpacked data block
    uint32 blockId; // solid block ID, this is the header index of the file that serves as solid block
    uint16 cipherWarmup; // if LVPAFLAG_ENCRYPTED is set, this many bytes were drawn from the cipher before starting the actual encryption
    uint8 flags; // see LVPAFileFlags
    uint8 algo; // algorithm used to compress this file
    uint8 level; // compression level used. default: LVPACOMP_INHERIT
    uint8 hash[LVPAHash_Size]; // used only if LVPAFLAG_SCRAMBLED is set

    // calculated during load, or only required for saving. not stored in the file.
    uint32 id;
    uint32 offset; // offset where the data block starts, either absolute address in the file, or offset in solid block
    memblock data;
    uint8 encryption;
    bool good;
    bool checkedCRC;
    bool checkedCRCPacked;

    // when a file from a solid block is requested, it gets a pointer to inside solid block's memory.
    // if the file is dropped later, its data.ptr is set to NULL to indicate it must be loaded again.
    // the spare ptr below will store this ptr to (1) quickly regain access to still existing memory,
    // and (2) to indicate a pointer to the solid block's memory does still exist and may be in use elsewhere.
    // Free() sets this to NULL.
    // only used for solid blocks.
    uint8 *sparePtr;

    // this is always true for solid files that are inside of a solid block (means if LVPAFileHeader.data.ptr points into another file's data.ptr
    // if so, we can't just delete[] the memory associated with this file.
    // if sparePtr is NULL and otherMem is true, memory came from outside and must not be touched.
    bool otherMem;

};

typedef std::map<std::string, uint32> LVPAIndexMap; // maps a file name to its internal file number (which is the index of _headers vector)

class MTRand;

class LVPAFile
{
public:
    LVPAFile();
    ~LVPAFile();
    bool LoadFrom(const char *fn, LVPALoadFlags loadFlags = LVPALOAD_NONE);
    virtual bool Save(LVPAComprLevels compression = LVPA_DEFAULT_LEVEL, LVPAAlgos algo = LVPAPACK_INHERIT, bool encrypt = false);
    virtual bool SaveAs(const char *fn, LVPAComprLevels compression = LVPA_DEFAULT_LEVEL, LVPAAlgos algo = LVPAPACK_INHERIT, bool encrypt = false);

    virtual void Add(const char *fn, memblock mb, const char *solidBlockName = NULL, uint8 algo = LVPAPACK_INHERIT,
        uint8 level = LVPACOMP_INHERIT, uint8 encrypt = LVPAENCR_INHERIT, bool scramble = false); // adds a file, overwriting if exists
    virtual memblock Remove(const char *fn); // removes a file from the container and returns its memblock
    memblock Get(const char *fn, bool checkCRC = true);
    memblock Get(uint32 index, bool checkCRC = true);
    uint32 GetId(const char *fn);
    void Clear(bool del = true); // free all
    virtual bool Delete(const char *fn); // removes a file from the container and frees up memory. returns false if the file was not found.

    // Frees the memory associated with a file, leaving it in the container. if requested again, it will be loaded from disk.
    // Returns true if the memory was freed, false if it was not possible.
    bool Free(const char *fn);
    bool Free(uint32 id);
    uint32 FreeUnused(void); // frees unused solid blocks

    // drops our reference to the file, so it will be loaded again from disk if required. The original memory will not be touched,
    // so that it can be processed elsewhere.
    // Returns true if the memory is no longer referenced anywhere inside the LVPAFile, and can be mangled freely.
    // If it returns false, the memory needs to be copied by hand.
    bool Drop(const char *fn);
    bool Drop(uint32 id);

    uint32 SetSolidBlock(const char *name, uint8 compression = LVPACOMP_INHERIT, uint8 algo = LVPAPACK_INHERIT); // return file id of block

    inline uint32 Count(void) const { return _indexes.size(); }
    inline uint32 HeaderCount(void) const { return _headers.size(); }
    const char *GetMyName(void) const { return _ownName.c_str(); }

    bool AllGood(void) const;
    const LVPAFileHeader& GetFileInfo(uint32 i) const;

    // encryption related
    void SetMasterKey(const uint8 *key, uint32 size);

    // for stats. note: only call these directly after load/save, and NOT after files were added/removed!
    inline uint32 GetRealSize(void) const { return _realSize; }
    uint32 GetPackedSize(void) const { return _packedSize; }

    void RandomSeed(uint32); // for encryption


private:
    std::string _ownName;
    LVPAIndexMap _indexes;
    std::vector<LVPAFileHeader> _headers;
    void *_handle; // its a FILE *
    MTRand *_mtrand;
    uint32 _realSize, _packedSize; // for stats

    std::vector<uint8> _masterKey; // used as global encryption key for each file
    uint8 _masterSalt[LVPAHash_Size]; // derived from master key, used for filename salting

    // loading functions, call chain/data flow is in this order:
    // [HDD] -> _LoadFile() -> _DecryptFile() -> _UnpackFile() -> _PrepareFile() -> Get() -> [memblock]
    bool _LoadFile(memblock& target, LVPAFileHeader& h); // load from disk
    bool _DecryptFile(memblock &target, LVPAFileHeader& h); // _LoadFile() and decrypt
    memblock _UnpackFile(LVPAFileHeader& h); // _DecryptFile(), and unpack
    memblock _PrepareFile(LVPAFileHeader& h, bool checkCRC = true); // _UnpackFile(), and check CRC

    bool _OpenFile(void);
    void _CloseFile(void);
    void _CreateIndexes(void); // load helper
    void _CalcOffsets(uint32 startOffset); // load helper
    void _MakeSolid(LVPAFileHeader& h, const char *solidBlockName); // put file into solid block
    void _CalcSaltedFilenameHash(uint8 *dst, const std::string& fn);
    // encrypt or decrypt block of data; it is assumed that hdr.filename already holds the correct file name in case the file is scrambled
    // writeMode should be true when the block is supposed to be encrypted/scrambled, false otherwise
    bool _CryptBlock(uint8 *buf, LVPAFileHeader& hdr, bool writeMode);
    // these return true and set *id to the internal file number (= _headers[] array position) if found
    bool _FindHeaderByName(const char *fn, uint32 *id);
    bool _FindHeaderByHash(uint8 *hash, uint32 *id);

};

class LVPAFileReadOnly : public LVPAFile
{
    virtual bool SaveAs(const char *fn, LVPAComprLevels compression = LVPA_DEFAULT_LEVEL, LVPAAlgos algo = LVPAPACK_INHERIT, bool encrypt = false) { return false; }
};

bool IsSupported(LVPAAlgos algo);

LVPA_NAMESPACE_END

#endif

