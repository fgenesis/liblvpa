#include "LVPACommon.h"
#include "LVPATests.h"
#include "LVPACipherTests.h"

#include <cstdio>

#define DO_TESTRUN(f) { printf("Running: %s\n", #f); int _r = (f); if(_r) { printf("TEST FAILED: Func %s returned %d\n", #f, _r); return 1; } }

int main(int argc, char *argv[])
{
    DO_TESTRUN(LVPATestsInit());

#ifdef LVPA_SUPPORT_LZO
    DO_TESTRUN(TestLZO());
#endif
#ifdef LVPA_SUPPORT_LZMA
    DO_TESTRUN(TestLZMA());
#endif
#ifdef LVPA_SUPPORT_ZLIB
    DO_TESTRUN(TestDeflate());
    DO_TESTRUN(TestZlib());
    DO_TESTRUN(TestGzip());
#endif
#ifdef LVPA_SUPPORT_LZF
    DO_TESTRUN(TestLZF());
#endif
#ifdef LVPA_SUPPORT_LZHAM
    DO_TESTRUN(TestLZHAM());
#endif

    DO_TESTRUN(TestRC4());
    DO_TESTRUN(TestHPRC4Like());
    DO_TESTRUN(TestHPRC4LikeBytewise());
    DO_TESTRUN(TestRC4Warm());
    DO_TESTRUN(TestHPRC4LikeWarm());
    //DO_TESTRUN(TestRC4Massive()); // RC4 not used and just kept for reference (is rather slow too)
    DO_TESTRUN(TestHPRC4LikeMassive());

    DO_TESTRUN(TestLVPAUncompressed());
#ifdef LVPA_SUPPORT_LZO
    DO_TESTRUN(TestLVPA_LZO());
#endif
#ifdef LVPA_SUPPORT_LZMA
    DO_TESTRUN(TestLVPA_LZMA());
#endif
#ifdef LVPA_SUPPORT_ZLIB
    DO_TESTRUN(TestLVPA_Deflate());
#endif
    DO_TESTRUN(TestLVPAUncompressedSolid());
    DO_TESTRUN(TestLVPA_MixedSolid());
    DO_TESTRUN(TestLVPAUncompressedEncrypted());
    DO_TESTRUN(TestLVPAUncompressedScrambled());
    DO_TESTRUN(TestLVPAUncompressedEncrScram());
    DO_TESTRUN(TestLVPA_Compr_EncrScram());
    DO_TESTRUN(TestLVPA_Everything());

    DO_TESTRUN(TestLVPA_CreateAndAppend1());
    DO_TESTRUN(TestLVPA_CreateAndAppend2());
    DO_TESTRUN(TestLVPA_CreateAndAppend3());
    DO_TESTRUN(TestLVPA_CreateAndAppend4());
    DO_TESTRUN(TestLVPA_CreateAndAppend5());

#ifdef LVPA_SUPPORT_TTVFS
    DO_TESTRUN(TestLVPA_VFS_Simple());
    DO_TESTRUN(TestLVPA_VFS_ScrambledLoader());
    DO_TESTRUN(TestLVPA_VFS_ScrambledLoaderEncrypted());
#endif

    printf("All tests successful!\n");

    return 0;
}
