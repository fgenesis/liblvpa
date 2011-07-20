#include "LVPAInternal.h"
#include <cstdio>

#include "LVPACommon.h"
#include "SHA256Hash.h"
#include "LZMACompressor.h"
#include "LZOCompressor.h"
#include "DeflateCompressor.h"
#include "LZFCompressor.h"
#include "LVPAFile.h"

#ifdef LVPA_NAMESPACE
  using namespace LVPA_NAMESPACE;
#endif

const char v0[] = "";
const char v1[] = "a";
const char v2[] = "aaaaaaaaaa";
const char v3[] = "aaBaaBaaBBBBBaaaaaBBBBBaaaaaaaaaaBBaaBBBBBBBBBB";
const char v4[] = "Short test string.";
const char v5[] = "Longer test string, longer because the string is longer, and the string is the test";
const char v6[] = "Long test string with many repetitions many repetitions many repetitions many repetitions many repetitions until many repetitions do end.";

const uint8 b1[] = 
{
    0,1,2,3,4,5,6,7,8,9,
    9,8,7,6,5,4,3,2,1,0,
    0,1,2,3,4,5,6,7,8,9,
    9,8,7,6,5,4,3,2,1,0,
    0,1,2,3,4,5,6,7,8,9,
    9,8,7,6,5,4,3,2,1,0,
    0,1,2,3,4,5,6,7,8,9,
    9,8,7,6,5,4,3,2,1,0,
    0,1,2,3,4,5,6,7,8,9,
    9,8,7,6,5,4,3,2,1,0,
};

const uint32 i1[] = 
{
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

const uint32 i2[] =
{
    0,1,10,100,1000,10000,100000,1000000,
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    0x100, 0x200, 0x400, 0x800
};

#define DO_PACK_UNPACK(mem, compr) \
{ \
    uint32 _size = sizeof(mem); \
    compr _c; _c.append((const char*)&mem[0], _size); \
    _c.Compress(); \
    printf("C %s [%s]: %u -> %u\n", #mem, #compr, _c.RealSize(), _c.size()); \
    _c.Decompress(); \
    if(memcmp((const char*)&mem[0], _c.contents(), _c.size())) return 1; \
}

#define DO_COMPRESS_RUN(compr) \
{ \
    DO_PACK_UNPACK(v0, compr); \
    DO_PACK_UNPACK(v1, compr); \
    DO_PACK_UNPACK(v2, compr); \
    DO_PACK_UNPACK(v3, compr); \
    DO_PACK_UNPACK(v4, compr); \
    DO_PACK_UNPACK(v5, compr); \
    DO_PACK_UNPACK(v6, compr); \
    DO_PACK_UNPACK(b1, compr); \
    DO_PACK_UNPACK(i1, compr); \
    DO_PACK_UNPACK(i2, compr); \
}

static LVPAEncrpytions g_encrypt = LVPAENCR_NONE;
static bool g_scramble = false;
static const char *g_blockName = "";
static uint8 g_masterKey[LVPAHash_Size];

#define INIT_TEST() \
{ \
    g_scramble = false; \
    g_encrypt = LVPAENCR_NONE; \
    g_blockName = NULL; \
}

#define MAKE_MEMBLOCK(mem) memblock((uint8*)&mem[0], sizeof(mem))

#define ADD_MEMBLOCK(mem) lvpa.Add("FILE_" #mem, MAKE_MEMBLOCK(mem), g_blockName, \
    LVPAPACK_INHERIT, LVPACOMP_INHERIT, g_encrypt, g_scramble)

#define DO_CHECK_SAME(mem) \
{ \
    memblock _m = lvpa.Get("FILE_" #mem); \
    if(!_m.ptr) return 1; \
    if(memcmp((const char*)&mem[0], (const char*)_m.ptr, _m.size)) return 2; \
}

#define DO_ADD_ALL() \
{ \
    ADD_MEMBLOCK(v0); \
    ADD_MEMBLOCK(v1); \
    ADD_MEMBLOCK(v2); \
    ADD_MEMBLOCK(v3); \
    ADD_MEMBLOCK(v4); \
    ADD_MEMBLOCK(v5); \
    ADD_MEMBLOCK(v6); \
    ADD_MEMBLOCK(b1); \
    ADD_MEMBLOCK(i1); \
    ADD_MEMBLOCK(i2); \
}

#define DO_CHECK_ALL() \
{ \
    DO_CHECK_SAME(v0); \
    DO_CHECK_SAME(v1); \
    DO_CHECK_SAME(v2); \
    DO_CHECK_SAME(v3); \
    DO_CHECK_SAME(v4); \
    DO_CHECK_SAME(v5); \
    DO_CHECK_SAME(v6); \
    DO_CHECK_SAME(b1); \
    DO_CHECK_SAME(i1); \
    DO_CHECK_SAME(i2); \
}

#define DO_ADD_CHECK_ALL() \
{ \
    DO_ADD_ALL(); \
    DO_CHECK_ALL(); \
}

#define DO_LOAD_AND_CHECK_ALL() \
{ \
    LVPAFile lvpa; \
    lvpa.SetMasterKey(&g_masterKey[0], LVPAHash_Size); \
    lvpa.LoadFrom("~test.lvpa.tmp"); \
    DO_CHECK_ALL(); \
}


int LVPATestsInit()
{
    const char *k = "All your base are belong to us.";
    SHA256Hash::Calc(&g_masterKey[0], (uint8*)k, strlen(k));

    return 0;
}

#ifdef LVPA_SUPPORT_LZMA
int TestLZMA()
{
    DO_COMPRESS_RUN(LZMACompressor);
    return 0;
}
#endif

#ifdef LVPA_SUPPORT_LZO
int TestLZO()
{
    DO_COMPRESS_RUN(LZOCompressor);
    return 0;
}
#endif

#ifdef LVPA_SUPPORT_ZLIB
int TestDeflate()
{
    DO_COMPRESS_RUN(DeflateCompressor);
    return 0;
}
int TestZlib()
{
    DO_COMPRESS_RUN(ZlibCompressor);
    return 0;
}
int TestGzip()
{
    DO_COMPRESS_RUN(GzipCompressor);
    return 0;
}
#endif

#ifdef LVPA_SUPPORT_LZF
int TestLZF()
{
    DO_COMPRESS_RUN(LZFCompressor);
    return 0;
}
#endif

int TestLVPAUncompressed()
{
    INIT_TEST();
    {
        LVPAFile lvpa;
        DO_ADD_CHECK_ALL();
        lvpa.SaveAs("~test.lvpa.tmp", 0);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPA_LZMA()
{
    INIT_TEST();
    {
        LVPAFile lvpa;
        DO_ADD_CHECK_ALL();
        lvpa.SaveAs("~test.lvpa.tmp", 3, LVPAPACK_LZMA);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPA_LZO()
{
    INIT_TEST();
    {
        LVPAFile lvpa;
        DO_ADD_CHECK_ALL();
        lvpa.SaveAs("~test.lvpa.tmp", LVPACOMP_FAST, LVPAPACK_LZO1X);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPA_Deflate()
{
    INIT_TEST();
    {
        LVPAFile lvpa;
        DO_ADD_CHECK_ALL();
        lvpa.SaveAs("~test.lvpa.tmp", LVPACOMP_GOOD, LVPAPACK_DEFLATE);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPAUncompressedSolid()
{
    INIT_TEST();
    g_blockName = ""; // solid block with empty name
    {
        LVPAFile lvpa;
        DO_ADD_CHECK_ALL();
        lvpa.SaveAs("~test.lvpa.tmp", 0);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPA_MixedSolid()
{
    INIT_TEST();
    {
        LVPAFile lvpa;
        g_blockName = NULL;
        ADD_MEMBLOCK(v0);
        ADD_MEMBLOCK(v1);
        ADD_MEMBLOCK(v2);
        g_blockName = "txt";
        ADD_MEMBLOCK(v3);
        ADD_MEMBLOCK(v4);
        ADD_MEMBLOCK(v5);
        ADD_MEMBLOCK(v6);
        g_blockName = "bin";
        ADD_MEMBLOCK(b1);
        ADD_MEMBLOCK(i1);
        ADD_MEMBLOCK(i2);
        lvpa.SetSolidBlock("txt", LVPACOMP_FASTEST, LVPAPACK_LZMA);
        lvpa.SetSolidBlock("bin", LVPACOMP_ULTRA, LVPAPACK_LZO1X);
        DO_CHECK_ALL();
        lvpa.SaveAs("~test.lvpa.tmp", 0); // leave headers uncompressed
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPAUncompressedEncrypted()
{
    INIT_TEST();
    g_encrypt = LVPAENCR_ENABLED;
    {
        LVPAFile lvpa;
        DO_ADD_CHECK_ALL();
        lvpa.SetMasterKey(&g_masterKey[0], LVPAHash_Size);
        lvpa.SaveAs("~test.lvpa.tmp", 0, 0, true);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPAUncompressedScrambled()
{
    INIT_TEST();
    g_scramble = true;
    {
        LVPAFile lvpa;
        DO_ADD_CHECK_ALL();
        lvpa.SetMasterKey(&g_masterKey[0], LVPAHash_Size);
        lvpa.SaveAs("~test.lvpa.tmp", 0, 0, true);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPAUncompressedEncrScram()
{
    INIT_TEST();
    g_encrypt = LVPAENCR_ENABLED;
    g_scramble = true;
    {
        LVPAFile lvpa;
        DO_ADD_CHECK_ALL();
        lvpa.SetMasterKey(&g_masterKey[0], LVPAHash_Size);
        lvpa.SaveAs("~test.lvpa.tmp", 0, 0, true);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPA_LZMA_EncrScram()
{
    INIT_TEST();
    g_encrypt = LVPAENCR_ENABLED;
    g_scramble = true;
    {
        LVPAFile lvpa;
        DO_ADD_CHECK_ALL();
        lvpa.SetMasterKey(&g_masterKey[0], LVPAHash_Size);
        lvpa.SaveAs("~test.lvpa.tmp", LVPACOMP_NORMAL, LVPAPACK_LZMA, true);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL();
    return 0;
}

int TestLVPA_Everything()
{
    INIT_TEST();
    {
        LVPAFile lvpa;
        lvpa.SetMasterKey(&g_masterKey[0], LVPAHash_Size);
        g_blockName = NULL;
        g_scramble = true;
        g_encrypt = LVPAENCR_ENABLED;
        ADD_MEMBLOCK(v0);
        ADD_MEMBLOCK(v1);
        ADD_MEMBLOCK(v2);
        g_blockName = "txt";
        g_scramble = false;
        g_encrypt = LVPAENCR_NONE;
        ADD_MEMBLOCK(v3);
        ADD_MEMBLOCK(v4);
        ADD_MEMBLOCK(v5);
        ADD_MEMBLOCK(v6);
        g_blockName = "bin";
        g_encrypt = LVPAENCR_ENABLED;
        ADD_MEMBLOCK(b1);
        ADD_MEMBLOCK(i1);
        ADD_MEMBLOCK(i2);
        lvpa.SetSolidBlock("txt", LVPACOMP_FASTEST, LVPAPACK_LZMA);
        lvpa.SetSolidBlock("bin", LVPACOMP_ULTRA, LVPAPACK_LZO1X);
        DO_CHECK_ALL();
        lvpa.SaveAs("~test.lvpa.tmp", LVPACOMP_NORMAL, LVPAPACK_LZO1X, true);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    DO_LOAD_AND_CHECK_ALL()
        return 0;
}

// --- Tests for ttvfs bindings ---

#ifdef  LVPA_SUPPORT_TTVFS

#include "VFS.h"
#include "VFSHelperLVPA.h"

#ifdef VFS_NAMESPACE
  using namespace VFS_NAMESPACE;
#endif

#define DO_CHECK_VFS(mem) \
{ \
    VFSFile *vf = vfs.GetFile("FILE_" #mem); \
    if(!vf) return 1; \
    if(memcmp((const char*)&mem[0], (const char*)vf->getBuf(), (size_t)vf->size())) return 3; \
}

int TestLVPA_VFS_ScrambledLoader()
{
    INIT_TEST()
    {
        LVPAFile lvpa;
        g_scramble = true;
        ADD_MEMBLOCK(v5);
        ADD_MEMBLOCK(v6);
        ADD_MEMBLOCK(b1);
        ADD_MEMBLOCK(i1);
        ADD_MEMBLOCK(i2);
        lvpa.SaveAs("~test.lvpa.tmp", LVPACOMP_NONE, LVPAPACK_NONE, false);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }
    VFSHelperLVPA vfs;
    LVPAFile lvpa;
    lvpa.LoadFrom("~test.lvpa.tmp");
    vfs.LoadBaseContainer(&lvpa, false);
    vfs.Prepare();
    DO_CHECK_VFS(v5);
    DO_CHECK_VFS(v6);
    DO_CHECK_VFS(b1);
    DO_CHECK_VFS(i1);
    DO_CHECK_VFS(i2);
    return 0;
}

int TestLVPA_VFS_ScrambledLoaderEncrypted()
{
    INIT_TEST()
    {
        LVPAFile lvpa;
        lvpa.SetMasterKey(&g_masterKey[0], LVPAHash_Size);
        g_scramble = true;
        g_encrypt = LVPAENCR_ENABLED;
        ADD_MEMBLOCK(v5);
        ADD_MEMBLOCK(v6);
        ADD_MEMBLOCK(b1);
        ADD_MEMBLOCK(i1);
        ADD_MEMBLOCK(i2);
        lvpa.SaveAs("~test.lvpa.tmp", LVPACOMP_ULTRA, LVPAPACK_LZO1X, true);
        lvpa.Clear(false); // otherwise we would attempt to delete const memory
    }

    VFSHelperLVPA vfs;
    LVPAFile lvpa;
    lvpa.SetMasterKey(&g_masterKey[0], LVPAHash_Size);

    lvpa.LoadFrom("~test.lvpa.tmp");
    vfs.LoadBaseContainer(&lvpa, false);
    vfs.Prepare();

    DO_CHECK_VFS(v5);
    DO_CHECK_VFS(v6);
    DO_CHECK_VFS(b1);
    DO_CHECK_VFS(i1);
    DO_CHECK_VFS(i2);
    return 0;
}

#endif // LVPA_SUPPORT_TTVFS
