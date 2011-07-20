#include "LVPAInternal.h"
#include "tools.h"

#include "ProgressBar.h"
#include "LVPAFile.h"
#include "MyCrc32.h"
#include "SHA256Hash.h"

#include <set>
#include <list>
#include <cctype>

#ifdef LVPA_NAMESPACE
using namespace LVPA_NAMESPACE;
#endif

// WARNING: The code in this file SUCKS. Please look away.


// TEMP HACK
#ifdef logerror
#undef logerror
#endif
#define logerror(...) {printf(__VA_ARGS__); putchar('\n');}

enum PackCmd
{
    PC_NONE,            // no action
    PC_LISTFILE_NEWLINE,// this resets some settings
    PC_ADD_FILE,        // normal file name
    PC_MAKE_SOLID,      // * -s<NAME>
    PC_NOT_SOLID,       // * -n
    PC_SET_COMPR,       // * -c<A><#>
    PC_SET_PATH,        // * -p <PATH>
    PC_SET_ENCRYPT,     // * -e
    PC_SET_SCRAMBLE,    // * -x
    PC_SET_SOLID_COMPR, //   -S<name>=<A><#>
    PC_SET_HDR_COMPR,   //   -H<name>=<A><#>
    PC_SET_HDR_ENCRYPT, //   -E
    PC_SET_KEY,         //   -K[b/h] <text>

    // things marked with * are reset on listfile line end
};

struct PackDef;

typedef void (*PackCmdExecutor)(LVPAFile *, PackDef *, PackDef *);

static void PackCmd_ListfileNewline(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_AddFile(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_MakeSolid(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_NotSolid(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_SetCompr(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_SetPath(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_SetSolidCompr(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_SetHdrCompr(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_SetHdrEncrypt(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_SetEncrypt(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_SetScramble(LVPAFile *lvpa, PackDef *d, PackDef *glob);
static void PackCmd_SetKey(LVPAFile *lvpa, PackDef *d, PackDef *glob);


struct PackDef
{
    PackDef()
        : level(LVPACOMP_INHERIT), algo(LVPAPACK_INHERIT), encrypt(LVPAENCR_INHERIT),
        solid(false), scramble(false), exec(NULL)
    {}

    PackDef(PackCmd cmd)
        : level(LVPACOMP_INHERIT), algo(LVPAPACK_INHERIT), encrypt(LVPAENCR_INHERIT),
        solid(false), scramble(false), exec(NULL)
    {
        init(cmd);
    }

    void init(PackCmd c)
    {
        cmd = c;
        switch(c)
        {
            case PC_NONE:               exec = NULL; break; // we should never call this then anyway
            case PC_LISTFILE_NEWLINE:   exec = &PackCmd_ListfileNewline; break;
            case PC_ADD_FILE:           exec = &PackCmd_AddFile; break;
            case PC_MAKE_SOLID:         exec = &PackCmd_MakeSolid; break;
            case PC_NOT_SOLID:          exec = &PackCmd_NotSolid; break;
            case PC_SET_COMPR:          exec = &PackCmd_SetCompr; break;
            case PC_SET_PATH:           exec = &PackCmd_SetPath; break;
            case PC_SET_SOLID_COMPR:    exec = &PackCmd_SetSolidCompr; break;
            case PC_SET_HDR_COMPR:      exec = &PackCmd_SetHdrCompr; break;
            case PC_SET_HDR_ENCRYPT:    exec = &PackCmd_SetHdrEncrypt; break;
            case PC_SET_ENCRYPT:        exec = &PackCmd_SetEncrypt; break;
            case PC_SET_SCRAMBLE:       exec = &PackCmd_SetScramble; break;
            case PC_SET_KEY:            exec = &PackCmd_SetKey; break;
            default:
                logerror("PackDef init switch ERROR");
        }
    }
    PackCmd cmd;
    PackCmdExecutor exec;
    std::string name;
    std::string relPath;
    std::string solidBlockName;
    std::string keyStr;
    uint8 level;    // LVPACOMP_INHERIT by default
    uint8 algo;     // LVPAPACK_INHERIT by default
    uint8 encrypt;  // LVPAENCR_INHERIT by default
    bool solid;     // this is needed because "" is also a valid solid block name
    
    bool scramble;
    bool useHash; // for SetKey
    bool useBinary; // for SetKey
    bool fromCmdLine;
};

// some globals
static uint8 g_hdrLevel = LVPACOMP_INHERIT;
static uint8 g_hdrAlgo = LVPAPACK_INHERIT;
static bool g_hdrEncr = false;
static uint8 g_mode = 0;
static uint32 g_filesDone = 0;
static std::string g_relPath;
static LVPAFile *g_lvpa = NULL;

static std::string g_currentDir; // this is constantly updated whenever directories are changed

static bool _IsFileMode(void)
{
    switch(g_mode) { case 'a': case 'c': case 'x': case 'e': return true; }
    return false;
}

static bool _IsAddMode(void)
{
    switch(g_mode) { case 'a': case 'c': return true; }
    return false;
}

static bool _IsExtractMode(void)
{
    switch(g_mode) { case 'x': case 'e': return true; }
    return false;
}

// strips a file name from a given path, or the last folder if the path doesnt end with '/' or '\'
static std::string _PathStripLast(std::string str)
{
    size_t pathend = str.find_last_of("/\\");
    if(pathend != std::string::npos)
    {
        return str.substr(0, pathend + 1);
    }
    return ""; // no / or \, strip all
}

// from SDL
void UnEscapeQuotes(char *arg)
{
    char *last = NULL;

    while (*arg) {
        if (*arg == '"' && *last == '\\') {
            char *c_curr = arg;
            char *c_last = last;

            while (*c_curr) {
                *c_last = *c_curr;
                c_last = c_curr;
                c_curr++;
            }
            *c_last = '\0';
        }
        last = arg;
        arg++;
    }
}

// from SDL
/* Parse a command line buffer into arguments */
static int ParseCommandLine(char *cmdline, char **argv)
{
    char *bufp;
    char *lastp = NULL;
    int argc, last_argc;

    argc = last_argc = 0;
    for (bufp = cmdline; *bufp;) {
        /* Skip leading whitespace */
        while (isspace(*bufp)) {
            ++bufp;
        }
        /* Skip over argument */
        if (*bufp == '"') {
            ++bufp;
            if (*bufp) {
                if (argv) {
                    argv[argc] = bufp;
                }
                ++argc;
            }
            /* Skip over word */
            lastp = bufp;
            while (*bufp && (*bufp != '"' || *lastp == '\\')) {
                lastp = bufp;
                ++bufp;
            }
        } else {
            if (*bufp) {
                if (argv) {
                    argv[argc] = bufp;
                }
                ++argc;
            }
            /* Skip over word */
            while (*bufp && !isspace(*bufp)) {
                ++bufp;
            }
        }
        if (*bufp) {
            if (argv) {
                *bufp = '\0';
            }
            ++bufp;
        }

        /* Strip out \ from \" sequences */
        if (argv && last_argc != argc) {
            UnEscapeQuotes(argv[last_argc]);
        }
        last_argc = argc;
    }
    if (argv) {
        argv[argc] = NULL;
    }
    return (argc);
}

static void HexStrToByteArray(uint8 *dst, const char *str)
{
    int l = strlen(str);
    char a, b;
    int hi, lo;
    // uneven digit count? treat as if there was another '0' char in front
    if(l & 1)
    {
        a = '0';
        b = *str++;
    }
    l /= 2; // if uneven, this rounds down correctly

    for(int i=0; i < l; i++)
    {
        a = *str++;
        b = *str++;

        if(isdigit(a))            hi = a - '0';
        else if(a>='A' && a<='F') hi = a - 'A' + 10;
        else if(a>='a' && a<='f') hi = a - 'a' + 10;
        else                      hi = 0;

        if(isdigit(b))            lo = b - '0';
        else if(b>='A' && b<='F') lo = b - 'A' + 10;
        else if(b>='a' && b<='f') lo = b - 'a' + 10;
        else                      lo = 0;

        dst[i] = (hi << 4) + lo;
    }
}

static void _AddFileToArchive(LVPAFile *lvpa, PackDef *glob, const std::string& diskFileName, const std::string& archiveFileName)
{
    logdebug("-> Add file '%s' [%s]", archiveFileName.c_str(), diskFileName.c_str());

    FILE *fh = fopen(diskFileName.c_str(), "rb");
    if(!fh)
    {
        logerror("Add mode: file not found: '%s'", diskFileName.c_str());
        return;
    }
    fseek(fh, 0, SEEK_END);
    uint32 s = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    uint8 *buf = new uint8[s];
    fread(buf, s, 1, fh);
    fclose(fh);

    lvpa->Add(archiveFileName.c_str(), memblock(buf, s), glob->solid ? glob->solidBlockName.c_str() : NULL,
        glob->algo, glob->level, glob->encrypt, glob->scramble);
}

static std::set<std::string> g_createdDirs;

static void _UnpackFileFromArchive(LVPAFile *lvpa, const std::string& diskFileName, const std::string& archiveFileName) 
{
    logdebug("-> Extract file '%s' [%s]", archiveFileName.c_str(), diskFileName.c_str());

    memblock mb = lvpa->Get(archiveFileName.c_str());
    if(!mb.ptr)
    {
        logerror("Extract mode: '%s' not in archive", archiveFileName.c_str());
        return;
    }

    std::string diskdir = _PathStripLast(diskFileName);
    // avoid attempting to create directories recursively for every file as soon as it was done once
    if(g_createdDirs.find(diskdir) == g_createdDirs.end())
    {
        if(CreateDirRec(diskdir.c_str()))
        {
            logdebug("Created output dir '%s'", diskdir.c_str());
        }
        else
        {
            logerror("Failed to create output dir '%s'", diskdir.c_str());
        }
        g_createdDirs.insert(diskdir);
    }

    FILE *fh = fopen(diskFileName.c_str(), "wb");
    if(!fh)
    {
        logerror("Extract mode: Can't write out '%s'", diskFileName.c_str());
        return;
    }

    uint32 written = fwrite(mb.ptr, 1, mb.size, fh);
    if(written != mb.size)
    {
        logerror("Extract mode: '%s' written incompletely - disk full?", diskFileName.c_str());
    }

    fclose(fh);
}

static void _DoFileByMode(LVPAFile *lvpa, PackDef *glob, const std::string& diskFileName, const std::string& archiveFileName) 
{
    std::string afile = FixSlashes(archiveFileName);
    switch(g_mode)
    {
        case 'a':
        case 'c':
            _AddFileToArchive(lvpa, glob, diskFileName, afile);
            break;

        case 'x':
        case 'e':
            _UnpackFileFromArchive(lvpa, diskFileName, afile);
            break;

        default:
            logerror("_DoFileByMode switch ERROR: %c", g_mode);
    }
    ++g_filesDone; // this is used to decide whether to extract all files, or if an explicit file list was given
}

// uses name & relPath only, rest is stolen from glob
static void PackCmd_AddFile(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsFileMode())
        return;
    std::string diskFileName = g_currentDir;
    MakeSlashTerminated(diskFileName);
    diskFileName += d->name;

    if(IsDirectory(diskFileName.c_str()))
    {
        logdebug("-> Adding directory '%s' recursively...", diskFileName.c_str());

        StringList filelist;
        GetFileListRecursive(diskFileName, filelist, true);

        for(StringList::iterator it = filelist.begin(); it != filelist.end(); ++it)
        {
            // *it == file name on disk
            std::string afile;
            if(g_relPath.size())
                afile += g_relPath + '/';
            if(glob->relPath.size())
                afile += glob->relPath + '/';
            if(d->relPath.size())
                afile += d->relPath + '/';
            afile += *it; // preserve actual subdirs

            _DoFileByMode(lvpa, glob, *it, afile);
        }
    }
    else
    {
        std::string archivedFileName;
        if(g_relPath.size())
            archivedFileName += g_relPath + '/';
        if(glob->relPath.size())
            archivedFileName += glob->relPath + '/';
        if(d->relPath.size())
            archivedFileName += d->relPath + '/';
        
        archivedFileName += PathToFileName(d->name.c_str());

        _DoFileByMode(lvpa, glob, diskFileName, archivedFileName);
    }
}

static void PackCmd_MakeSolid(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsAddMode())
        return;
    logdebug("-> Solid block is now '%s'", d->solidBlockName.c_str());
    glob->solid = true;
    glob->solidBlockName = d->solidBlockName;
}

static void PackCmd_NotSolid(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsAddMode())
        return;
    logdebug("-> Non-solid mode", d->solidBlockName.c_str());
    glob->solid = false;
    glob->solidBlockName = "<ERROR>"; // if this is put into a file, things are wrong
}

static void PackCmd_SetCompr(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsAddMode())
        return;
    logdebug("-> Set compression algo=%u, level=%u", uint32(d->algo), uint32(d->level));
    glob->algo = d->algo;
    glob->level = d->level;
}

static void PackCmd_SetPath(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsFileMode())
        return;
    logdebug("-> Relative path: '%s'", d->relPath.c_str());

    // special case if we got the -p PATH directly from command line
    // -- if so, we want to keep the path separated from other paths that listfiles may specify,
    //    to be able to unpack into another directory, for example
    if(d->fromCmdLine)
    {
        if(g_mode == 'a' || g_mode == 'c')
            g_relPath = d->relPath;
        else if(g_mode == 'x' || g_mode == 'e')
            g_currentDir = d->relPath;
        else
            logerror("Unused param -p in mode '%x'", g_mode);
    }
    else
        glob->relPath = d->relPath;
}

static void PackCmd_SetSolidCompr(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsAddMode())
        return;
    logdebug("-> Block '%s' compression algo=%u, level=%u", d->solidBlockName.c_str(), uint32(d->algo), uint32(d->level));
    lvpa->SetSolidBlock(d->solidBlockName.c_str(), d->level, d->algo);
}

static void PackCmd_SetHdrCompr(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsAddMode())
        return;
    logdebug("-> Header compression algo=%u, level=%u", uint32(d->algo), uint32(d->level));
    g_hdrAlgo = d->algo;
    g_hdrLevel = d->level;
}

static void PackCmd_SetHdrEncrypt(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsAddMode())
        return;
    logdebug("-> Header encrypt ON");
    g_hdrEncr = true;
}

static void PackCmd_ListfileNewline(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsFileMode())
        return;
    logdebug("-> Listfile line end, settings reset");
    glob->algo = LVPAPACK_INHERIT;
    glob->level = LVPACOMP_INHERIT;
    glob->relPath = "";
    glob->solid = false;
    glob->solidBlockName = "";
    glob->encrypt = false;
    glob->scramble = false;
}

static void PackCmd_SetEncrypt(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsAddMode())
        return;
    logdebug("-> Set encrypt: %u", d->encrypt);
    glob->encrypt = d->encrypt;
}

static void PackCmd_SetScramble(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    if(!_IsAddMode())
        return;
    logdebug("-> Set scramble: %u", d->scramble);
    glob->scramble = d->scramble;
}

static void PackCmd_SetKey(LVPAFile *lvpa, PackDef *d, PackDef *glob)
{
    // all modes
    logdebug("-> Set key: %s", d->keyStr.c_str());
    if(d->useHash)
    {
        uint8 mem[LVPAHash_Size];
        if(d->useBinary)
        {
            std::vector<uint8> keybuf((d->keyStr.size() + 1) / 2);
            HexStrToByteArray(&keybuf[0], d->keyStr.c_str());
            LVPAHash::Calc(&mem[0], &keybuf[0], keybuf.size());
        }
        else
        {
            LVPAHash::Calc(&mem[0], (uint8*)d->keyStr.c_str(), d->keyStr.size());
        }
        lvpa->SetMasterKey(&mem[0], LVPAHash_Size);
    }
    else if(d->useBinary)
    {
        std::vector<uint8> keybuf((d->keyStr.size() + 1) / 2);
        HexStrToByteArray(&keybuf[0], d->keyStr.c_str());
        lvpa->SetMasterKey(&keybuf[0], keybuf.size());
    }
    else
    {
        lvpa->SetMasterKey((uint8*)d->keyStr.c_str(), d->keyStr.size());
    }
}

static void dep_authors(void)
{
    puts("** lvpak uses:"
#ifdef LVPA_SUPPORT_LZMA
         " - LZMA by Igor Pavlov"
#endif
#ifdef LVPA_SUPPORT_LZO
         " - LZO by Markus F.X.J. Oberhumer"
#endif
#ifdef LVPA_SUPPORT_ZLIB
         " - zlib by Jean-loup Gailly and Mark Adler"
#endif
#ifdef LVPA_SUPPORT_LZF
         " - LZF by Marc Lehmann"
#endif
         " **");
}

static void usage(void)
{
    puts("lvpak MODE archive [-flags files/dirs ..]\n"
           "\n"
           "Modes:\n"
           "  c - create new archive (overwrite if exist)\n"
           "  a - append to archive or create new\n"
         //"  d - delete file or directory inside archive\n"
           "  e - extract to current directory\n"
           "  x - extract with full path\n"
           "  t - test archive\n"
           "  l - list files, mode, and stats\n"
         //"  r - repack with a different compression level\n"
           "\n"
           "Flags:\n"
           "  -p <PATH> - use PATH as relative path to prepend each file, or as outdir.\n"   // PC_SET_PATH
         //"  -d DIR - add all files inside directory\n"
         //"  -D DIR - same as -d, but recursive\n"
           "  -c<A><#> - default compression level and algorithm\n"                              // PC_SET_COMPR
           "     A - can be none"
#ifdef LVPA_SUPPORT_LZMA
           ", lzma"
#endif
#ifdef LVPA_SUPPORT_LZO
           ", lzo"
#endif
#ifdef LVPA_SUPPORT_ZLIB
           ", zip"
#endif
#ifdef LVPA_SUPPORT_LZF
           ", lzf"
#endif
           ", or i (inherit)\n"
           "     # - a number in 0..9 or i (inherit)\n"
           "  -f <FILE> - use a listfile\n"                                                      // processed inline
           "  -s<NAME> - put the following files into a solid block with name NAME.\n"           // PC_MAKE_SOLID
           "             (NAME can be empty)\n"
           "  -n - not solid block. This disables -s.\n"                                         // PC_NOT_SOLID
           "  -e[0] - encrypt the following files. -e0 turns off encryption.\n"                  // PC_SET_ENCRYPT
           "  -x[0] - scramble the following files. -x0 turns off scrambling.\n"                 // PC_SET_SCRAMBLE
           "  -v - be verbose\n"                                                                 // processed inline
           "  -S<NAME>=<A><#> - choose compression params for solid block (see -c, -s).\n"       // PC_SET_SOLID_COMPR
           "  -H<A><#> - choose compression params for headers (see -c).\n"                      // PC_SET_HDR_COMPR
           "  -E - turn on header encryption\n"                                                  // PC_SET_HDR_ENCRYPT
           "  -K[bh] <key> - set key required for de/encryption. Req. if -e/-E is used!\n"       // PC_SET_KEY & processed inline if seen on cmdline
           "     b  - if -Kb is used, treat the key as hexadecimal binary string.\n"
           "      h - use not the string, but the SHA256 hash of it.\n"
           "     bh - treat as hex string and hash it.\n"
           "\n"
           "<archive> is the archive file to create/modify/read\n"
           "<files> is a list of files to add; directories are added recursively.\n"
           "<files> and <flags> can be mixed, to apply different settings to many files.\n"
           "\n"
           "If files have no explicit compression/encryption settings,\n"
           "they will be inherited from the headers.\n"
           "Note that LZMA level >= 7 takes large amounts of memory and time!\n"
           "\n"
           "Examples: lvpak c -Hlzo9 -E -Kbh secret file01.txt file02...\n"
           "          lvpak x arch.lvpa -p extractdir\n"
           );
    dep_authors();
}

static void unknown(char *what)
{
    printf("Unknown parameter: '%s'\n", what);
}

// parses for example -c0 (no compression), -cnone, -c9 (max. default algo), -clzma (default lzma), -clzma9 (max lzma), -clzo3 (fast lzo), etc
// also supports -ci9 (use max level of inherited compression), -ci (inherit everything) -clzoi (use lzo, but inherit compression level)
// this function is used for -H, -S, -c.
static bool parseCompressString(const char *str, uint8 *algo, uint8 *level)
{
    uint32 l = strlen(str);

    if(!l)
        goto inherit_all; // -----> [GOTO]

    if(l == 1)
    {
        if(isdigit(str[0]))
        {
            *level = atoi(str);
            if(algo)
                *algo = LVPAPACK_INHERIT; // this also serves as default if nothing else can be chosen
            return true;
        }
        else if(str[0] == 'i')
        {
            inherit_all: // <------ [GOTO]
            *level = LVPACOMP_INHERIT;
            if(algo)
                *algo = LVPAPACK_INHERIT;
            return true;
        }
    }

    if(algo)
    {
        if(l)
        {
            if(str[0] == 'i')
            {
                *algo = LVPAPACK_INHERIT;
                *level = LVPAPACK_INHERIT;
                return parseCompressString(str + 1, NULL, level);
            }

            if(l >= 3)
            {
#ifdef LVPA_SUPPORT_LZO
                if(!strnicmp(str, "lzo", 3))
                {
                    *algo = LVPAPACK_LZO1X;
                    *level = LVPAPACK_INHERIT;
                    return parseCompressString(str + 3, NULL, level);
                }
#endif
#ifdef LVPA_SUPPORT_ZLIB
                if(!strnicmp(str, "zip", 3))
                {
                    *algo = LVPAPACK_DEFLATE;
                    *level = LVPAPACK_INHERIT;
                    return parseCompressString(str + 3, NULL, level);
                }
#endif
#ifdef LVPA_SUPPORT_LZF
                if(!strnicmp(str, "lzf", 3))
                {
                    *algo = LVPAPACK_LZF;
                    *level = LVPAPACK_INHERIT;
                    return parseCompressString(str + 3, NULL, level);
                }
#endif
                // ...

                if(l >= 4)
                {
#ifdef LVPA_SUPPORT_LZMA
                    if(!strnicmp(str, "lzma", 4))
                    {
                        *algo = LVPAPACK_LZMA;
                        *level = LVPAPACK_INHERIT;
                        return parseCompressString(str + 4, NULL, level);
                    }
#endif
                    if(!strnicmp(str, "none", 4))
                    {
                        *algo = LVPAPACK_NONE;
                        *level = LVPAPACK_NONE;
                        return true;
                    }
                }
            }
        }
    }

    logerror("malformed/invalid compression parameters: '%s'", str);
    return false;
}

static bool parseSingleCmd(char **argv, uint32 available, PackDef &pd, uint32& skip)
{
    const char *str = argv[0];
    DEBUG(ASSERT(str[0] == '-')); // because thats the only reason we enter this function
    ++str; // skip '-'

    switch(str[0])
    {
        case 'p': // may be given as "-pPATH" or "-p PATH"
        {
            if(str[1]) // if we have -p PATH, this is the null terminator, otherwise the first char of the path
            {
                pd.relPath = str + 1; // skip "-p" then
            }
            else if(available) // its next arg in array
            {
                pd.relPath = argv[1];
                skip = 1;
            }
            else
            {
                printf("Error: -p expects a path\n");
                return false;
            }
            pd.relPath = FixSlashes(pd.relPath);
            pd.init(PC_SET_PATH);
            return true;
        }

        case 'c':
        {
            if(!parseCompressString(str + 1, &pd.algo, &pd.level)) // skip "-c"
                return false;

            pd.init(PC_SET_COMPR);
            return true;
        }

        case 's':
        {
            pd.solidBlockName = str + 1;
            pd.init(PC_MAKE_SOLID);
            return true;
        }

        case 'H':
        {
            if(!parseCompressString(str + 1, &pd.algo, &pd.level)) // skip "-H"
                return false;

            pd.init(PC_SET_HDR_COMPR);
            return true;
        }

        case 'v':
            //log_setloglevel(3); // TODO FIXME
            return false;

        case 'S':
        {
            ++str; // skip "-S"
            char *eqpos = (char*)strchr(str, '=');
            if(!eqpos)
            {
                logerror("-S format: -S<name>=<A><#>  (see -c and -s for info)");
                return false;
            }
            *eqpos = 0;
            pd.solidBlockName = str;
            if(!parseCompressString(eqpos + 1, &pd.algo, &pd.level))
                return false;

            pd.init(PC_SET_SOLID_COMPR);
            return true;
        }

        case 'e':
        {
            ++str; // skip "-e"
            pd.encrypt = (str[0] != '0'); // if its just -e, this is \0
            pd.init(PC_SET_ENCRYPT);
            return true;
        }

        case 'x':
        {
            ++str; // skip "-x"
            pd.scramble = (str[0] != '0'); // if its just -x, this is \0
            pd.init(PC_SET_SCRAMBLE);
            return true;
        }

        case 'K':
        {
            char c;
            pd.useBinary = false;
            pd.useHash = false;
            while(c = *str++) // assignment is intentional
            {
                if(c == 'b')
                    pd.useBinary = true;
                if(c == 'h')
                    pd.useHash = true;
            }

            pd.keyStr = argv[1];
            skip = 1;
            pd.init(PC_SET_KEY);

            if(pd.fromCmdLine) // execute directly if seen on cmdline, required so that encrypted archives can be opened
                PackCmd_SetKey(g_lvpa, &pd, NULL);
            
            return true;
        }

        case 'E':
        {
            pd.init(PC_SET_HDR_ENCRYPT);
            return true;
        }

        default:
            unknown(argv[0]);
    }

    return false;
}

static void processListfile(const char *listfile, std::list<PackDef>& cmds);

static void parseArgv(std::list<PackDef>& cmds, uint32 argc, char **argv, bool isCmdLine)
{
    for(uint32 i = 0; i < argc; ++i)
    {
        char *p = argv[i];

        if(p[0] == '-')
        {
            uint32 available = argc - i;

            if(p[1] == 'f')
            {
                const char *listfile;
                if(p[2]) // if we have -f FILE, this is the null terminator, otherwise the first char of the path
                    listfile = p + 2; // skip "-f" then
                else if(available) // its next arg in array
                    listfile = argv[++i];

                processListfile(listfile, cmds);
            }
            else
            {
                PackDef pd;
                pd.fromCmdLine = isCmdLine;
                uint32 skip = 0;
                if(parseSingleCmd(argv + i, available, pd, skip))
                {
                    ASSERT(pd.exec != NULL);
                    cmds.push_back(pd);
                    i += skip; // in case we used more then 1 array entry
                }
            }
        }
        else
        {
            PackDef pd(PC_ADD_FILE);
            pd.fromCmdLine = isCmdLine;
            pd.name = g_currentDir + p; // is automatically truncated to file name later
            pd.relPath = _PathStripLast(p);
            cmds.push_back(pd);
        }
    }
}

static void parseArgString(std::list<PackDef>& cmds, const char *cstr)
{
    char *str = strdup(cstr);
    uint32 used = ParseCommandLine(str, NULL);
    std::vector<char*> argvv;
    argvv.resize(used + 1);
    ParseCommandLine(str, &argvv[0]); // we need only pointers anyways...

    parseArgv(cmds, used, &argvv[0], false);

    free(str);
}

static void processListfile(const char *listfile, std::list<PackDef>& cmds)
{
    FILE *fh = fopen(listfile, "r");
    if(!fh)
    {
        printf("Failed to open listfile '%s'\n", listfile);
        return;
    }

    std::string old_g_currentDir = g_currentDir;
    g_currentDir = _PathStripLast(listfile);

    uint32 size;

    fseek(fh, 0, SEEK_END);
    size = ftell(fh);
    fseek(fh, 0 , SEEK_SET);

    if(!size)
    {
        fclose(fh);
        return; // nothing to do
    }

    char *buf = new char[size + 1];
    size = fread(buf, 1, size, fh);
    buf[size] = 0;
    fclose(fh);

    std::vector<std::string> lines;
    StrSplit(buf, "\n\r", lines, false);

    for(uint32 i = 0; i < lines.size(); ++i)
    {
        const char *ptr = lines[i].c_str();
        if(*ptr)
        {
            PackDef pd(PC_LISTFILE_NEWLINE);
            cmds.push_back(pd);
        }
        if(uint32 len = lines[i].size())
        {
            if(ptr[0] == '#')
            {
                if(len > 9 && !strnicmp("include ", &ptr[1], 8)) // check for '#include FILENAME'
                {
                    std::string path = _PathStripLast(listfile);
                    std::string subListFile(path == listfile ? "" : path);
                    subListFile += &ptr[9];
                    processListfile(subListFile.c_str(), cmds);
                }
                continue;
            }
            
            parseArgString(cmds, ptr);
        }
    }

    g_currentDir = old_g_currentDir;

    delete [] buf;
}

static void processPackDefList(LVPAFile& lvpa, std::list<PackDef>& cmds, PackDef& glob, ProgressBar *bar = NULL, uint32 *counter = NULL)
{
    for(std::list<PackDef>::iterator it = cmds.begin(); it != cmds.end(); ++it)
    {
        PackDef& d = *it;
        d.exec(&lvpa, &d, &glob);
        
        if(bar)
        {
            bar->done = *counter;
            bar->Update();
        }
    }
    if(bar)
    {
        bar->Reset();
        bar->Finalize();
    }
}

static bool _LoadLVPA(LVPAFile& lvpa, const std::string& archive)
{
    if(g_mode != 'c') // in every other mode an existing file must be opened
    {
        bool loaded = lvpa.LoadFrom(archive.c_str());
        if(!loaded && g_mode != 'a') // ...but in add/append mode, it does not have to exist actually
        {
            logerror("Error opening archive: '%s'", archive.c_str());
            return false;
        }
    }
    return true;
}


int main(int argc, char *argv[])
{
    std::string archive; // archive file name
    std::list<PackDef> cmds; // filled when parsing cmd line and listfiles
    bool result = false; // true if everything went well

    // we need at least MODE & ARCHIVE - that makes 3 with exe name
    if(argc < 3)
    {
        usage();
        return 2;
    }

    if(argv[1][0] && !argv[1][1]) // mode may be 1 single char only, otherwise ignore, and error out
        g_mode = argv[1][0];

    archive = argv[2];

    if(!g_mode)
    {
        logerror("Invalid mode!");
        return 2;
    }

    if(archive.empty())
    {
        printf("No target archive file specified!\n");
        return 3;
    }

    LVPAFile lvpa;
    g_lvpa = &lvpa; // HACK

    parseArgv(cmds, argc - 3, argv + 3, true);

    if(!_LoadLVPA(lvpa, archive))
        return 1;

    PackDef glob; // holds current global settings

    switch(g_mode)
    {
        case 'l':
        {
            processPackDefList(lvpa, cmds, glob);
            for(uint32 i = 0; i < lvpa.HeaderCount(); ++i)
            {
                const LVPAFileHeader& h = lvpa.GetFileInfo(i);
                char lvlc;
                switch(h.level)
                {
                    case LVPACOMP_INHERIT: lvlc = 'i'; break;
                    default:               lvlc = '0' + h.level;
                }
                const char *algoStr = "UNK";
                switch(h.algo)
                {
                    case LVPAPACK_NONE:    algoStr = "None"; break;
                    case LVPAPACK_LZMA:    algoStr = "Lzma"; break;
                    case LVPAPACK_LZO1X:   algoStr = "Lzo "; break;
                    case LVPAPACK_DEFLATE: algoStr = "Zip "; break;
                    case LVPAPACK_LZF:     algoStr = "Lzf "; break;
                }
                printf("[%c%c%c%c,%s%c%s%s] '%s' (%u KB, %.2f%%)%s\n",
                    h.flags & LVPAFLAG_PACKED ? 'P' : '-',
                    h.flags & LVPAFLAG_SOLID ? 'S' : (h.flags & LVPAFLAG_SOLIDBLOCK ? '#' : '-'),
                    h.flags & LVPAFLAG_ENCRYPTED ? 'E' : '-',
                    h.flags & LVPAFLAG_SCRAMBLED ? 'X' : '-',
                    algoStr,
                    lvlc,
                    h.flags & LVPAFLAG_SOLID ? "," : "",
                    h.flags & LVPAFLAG_SOLID ? lvpa.GetFileInfo(h.blockId).filename.c_str() : "",
                    h.flags & LVPAFLAG_SCRAMBLED ? "?#scrambled#?" : h.filename.c_str(), // otherwise it would be empty anyways
                    h.realSize >> 10,
                    (float(h.packedSize) / float(h.realSize)) * 100.0f,
                    h.good ? "" : " (ERROR)");
                lvpa.Free(i);
            }
            printf("%u files present.\n", lvpa.HeaderCount());
            printf("Total compression ratio: %u KB -> %u KB (%.2f%%)\n", lvpa.GetRealSize() >> 10, lvpa.GetPackedSize() >> 10,
                 float(lvpa.GetPackedSize()) / float(lvpa.GetRealSize()) * 100.0f);
            result = true;
            break;
        }

        case 'a':
        case 'c':
        {
            if(cmds.empty())
            {
                printf("Add mode: Nothing to do\n");
                break;
            }
            {
                uint32 filesGiven = 0;
                for(std::list<PackDef>::iterator it = cmds.begin(); it != cmds.end(); ++it)
                    if(it->cmd == PC_ADD_FILE)
                        ++filesGiven;
                ProgressBar bar(filesGiven);
                bar.msg = "Preparing ... ";
                bar.Reset();
                bar.Update(true);
                processPackDefList(lvpa, cmds, glob, &bar, &g_filesDone);
            }

            // if nothing else specified, use for the header the same compression used for other files
            if(g_hdrAlgo == LVPAPACK_INHERIT)
                g_hdrAlgo = glob.algo;
            if(g_hdrLevel == LVPACOMP_INHERIT)
                g_hdrLevel = glob.level;

            result = lvpa.SaveAs(archive.c_str(), g_hdrLevel, g_hdrAlgo, g_hdrEncr);
            if(result)
            {
                uint32 real_kb = lvpa.GetRealSize() >> 10;
                uint32 packed_kb = lvpa.GetPackedSize() >> 10;
                printf("%u files packed.\n", g_filesDone);
                printf("Total compression ratio: %u KB -> %u KB (%.2f%%)\n", real_kb, packed_kb,
                    float(lvpa.GetPackedSize()) / float(lvpa.GetRealSize()) * 100.0f);
            }
            break;
        }

        case 't':
        {
            processPackDefList(lvpa, cmds, glob);
            result = true;
            {
                ProgressBar bar(lvpa.HeaderCount());
                bar.msg = "Testing ...";
                bar.Reset();
                bar.Update(true);
                for(uint32 i = 0; i < lvpa.HeaderCount(); ++i)
                {
                    const LVPAFileHeader &h = lvpa.GetFileInfo(i);
                    if(h.flags & (LVPAFLAG_SOLIDBLOCK | LVPAFLAG_SCRAMBLED))
                        continue;

                    memblock mb = lvpa.Get(i);
                    result = mb.ptr && result;
                    lvpa.Free(i);
                    bar.Step();
                }
                bar.Finalize();
            }

            result = result && lvpa.AllGood();
            printf("%s", result ? "File is OK\n" : "File is damaged, use 'lvpak l' to list\n");
            break;
        }

        case 'x':
        case 'e':
        {
            // no file name was given on the command line, means extract all
            // so add all from the archive to the list and continue as usual
            uint32 filesGiven = 0;
            for(std::list<PackDef>::iterator it = cmds.begin(); it != cmds.end(); ++it)
                if(it->cmd == PC_ADD_FILE)
                    ++filesGiven;

            if(!filesGiven)
            {
                for(uint32 i = 0; i < lvpa.HeaderCount(); ++i)
                {
                    const LVPAFileHeader &h = lvpa.GetFileInfo(i);
                    if(h.flags & LVPAFLAG_SOLIDBLOCK)
                        continue;
                    if(!h.filename.length())
                    {
                        printf("Can't extract file #%u, unknown or scrambled file name\n", i);
                        continue;
                    }

                    PackDef pd;
                    pd.name = h.filename; // the path will be stripped from this when it is processed

                    // mode 'x' extracts into subdirs, mode 'e' extracts without paths
                    if(g_mode == 'x')
                        pd.relPath = _PathStripLast(h.filename);
                    pd.init(PC_ADD_FILE);
                    cmds.push_back(pd);
                    ++filesGiven;
                }
            }

            ProgressBar bar(filesGiven);
            bar.msg = "Extracting: ";
            bar.Reset();
            bar.Update(true);
            processPackDefList(lvpa, cmds, glob, &bar, &g_filesDone);
            result = true;
            printf("%u files extracted.\n", g_filesDone);
            break;
        }


        default:
            logerror("Unsupported mode: '%c'\n", g_mode);
            result = false;
    }

    lvpa.Clear();
    return result ? 0 : 1;
}

