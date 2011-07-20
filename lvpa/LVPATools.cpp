#include "LVPAInternal.h"
#include "LVPATools.h"

#include <algorithm>
#include <cctype>
#include <stack>


#if PLATFORM == PLATFORM_WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <direct.h>
#else
#   include <sys/dir.h>
#   include <sys/stat.h>
#   include <sys/types.h>
#   include <sys/ioctl.h>
#   include <unistd.h>
#endif

LVPA_NAMESPACE_START

std::string stringToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), tolower);
    return s;
}

std::string stringToUpper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), toupper);
    return s;
}

void makeLowercase(std::string& s)
{
    std::transform(s.begin(), s.end(), s.begin(), tolower);
}

void makeUppercase(std::string& s)
{
    std::transform(s.begin(), s.end(), s.begin(), toupper);
}

// returns list of *plain* file names in given directory,
// without paths, and without anything else
void GetFileList(const char *path, StringList& files)
{
#if !_WIN32
    DIR * dirp;
    struct dirent * dp;
    dirp = opendir(path);
    if(dirp)
    {
        while((dp=readdir(dirp)) != NULL)
        {
            if (dp->d_type != DT_DIR) // only add if it is not a directory
            {
                std::string s(dp->d_name);
                files.push_back(s);
            }
        }
        closedir(dirp);
    }

# else

    WIN32_FIND_DATA fil;
    std::string search(path);
    MakeSlashTerminated(search);
    search += "*";
    HANDLE hFil = FindFirstFile(search.c_str(),&fil);
    if(hFil != INVALID_HANDLE_VALUE)
    {
        do
        {
            if(!(fil.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::string s(fil.cFileName);
                files.push_back(s);
            }
        }
        while(FindNextFile(hFil, &fil));

        FindClose(hFil);
    }

# endif
}

// returns a list of directory names in the given directory, *without* the source dir.
// if getting the dir list recursively, all paths are added, except *again* the top source dir beeing queried.
void GetDirList(const char *path, StringList &dirs, bool recursive /* = false */)
{
#if !_WIN32
    DIR * dirp;
    struct dirent * dp;
    dirp = opendir(path);
    if(dirp)
    {
        while((dp = readdir(dirp))) // assignment is intentional
        {
            if (dp->d_type == DT_DIR) // only add if it is a directory
            {
                if(strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0)
                {
                    dirs.push_back(dp->d_name);
                    if (recursive) // needing a better way to do that
                    {
                        std::deque<std::string> newdirs;
                        GetDirList(dp->d_name, newdirs, true);
                        std::string d(dp->d_name);
                        for(std::deque<std::string>::iterator it = newdirs.begin(); it != newdirs.end(); ++it)
                            dirs.push_back(d + *it);
                    }
                }
            }
        }
        closedir(dirp);
    }

#else

    std::string search(path);
    MakeSlashTerminated(search);
    search += "*";
    WIN32_FIND_DATA fil;
    HANDLE hFil = FindFirstFile(search.c_str(),&fil);
    if(hFil != INVALID_HANDLE_VALUE)
    {
        do
        {
            if( fil.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
            {
                if (!strcmp(fil.cFileName, ".") || !strcmp(fil.cFileName, ".."))
                    continue;

                std::string d(fil.cFileName);
                dirs.push_back(d);

                if (recursive) // need a better way to do that
                {
                    StringList newdirs;
                    GetDirList(d.c_str(), newdirs, true);

                    for(std::deque<std::string>::iterator it = newdirs.begin(); it != newdirs.end(); ++it)
                        dirs.push_back(d + *it);
                }
            }
        }
        while(FindNextFile(hFil, &fil));

        FindClose(hFil);
    }

#endif
}

void GetFileListRecursive(std::string dir, StringList& files, bool withQueriedDir /* = false */)
{
    std::stack<std::string> stk;

    if(withQueriedDir)
    {
        stk.push(dir);
        while(stk.size())
        {
            dir = stk.top();
            stk.pop();
            MakeSlashTerminated(dir);

            StringList li;
            GetFileList(dir.c_str(), li);
            for(std::deque<std::string>::iterator it = li.begin(); it != li.end(); ++it)
                files.push_back(dir + *it);

            li.clear();
            GetDirList(dir.c_str(), li, true);
            for(std::deque<std::string>::iterator it = li.begin(); it != li.end(); ++it)
                stk.push(dir + *it);
        }
    }
    else
    {
        std::string topdir = dir;
        MakeSlashTerminated(topdir);
        stk.push("");
        while(stk.size())
        {
            dir = stk.top();
            stk.pop();
            MakeSlashTerminated(dir);

            StringList li;
            dir = topdir + dir;
            GetFileList(dir.c_str(), li);
            for(std::deque<std::string>::iterator it = li.begin(); it != li.end(); ++it)
                files.push_back(dir + *it);

            li.clear();
            GetDirList(dir.c_str(), li, true);
            for(std::deque<std::string>::iterator it = li.begin(); it != li.end(); ++it)
                stk.push(dir + *it);
        }
    }
}

bool FileExists(const char *fn)
{
#ifdef _WIN32
    FILE *fp = fopen(fn, "rb");
    if(fp)
    {
        fclose(fp);
        return true;
    }
    return false;
#else
    return access(fn, F_OK) == 0;
#endif
}

// must return true if creating the directory was successful
bool CreateDir(const char *dir)
{
    if(IsDirectory(dir))
        return true;
	bool result;
# ifdef _WIN32
	result = !!::CreateDirectory(dir,NULL);
# else
	result = !mkdir(dir,S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
	return result;
}

bool CreateDirRec(const char *dir)
{
    if(IsDirectory(dir))
        return true;
    bool result = true;
    StringList li;
    StrSplit(dir, "/\\", li, false);
    std::string d;
    d.reserve(strlen(dir));
    bool last;
    for(StringList::iterator it = li.begin(); it != li.end(); it++)
    {
        d += *it;
        last = CreateDir(d.c_str());
        result = last && result;
        d += '/';
    }
    return result || last;
}

std::string FixSlashes(const std::string& s)
{
    std::string r;
    r.reserve(s.length() + 1);
    char last = 0, cur;
    for(size_t i = 0; i < s.length(); ++i)
    {
        cur = s[i];
        if(cur == '\\')
            cur = '/';
        if(last == '/' && cur == '/')
            continue;
        r += cur;
        last = cur;
    }
    return r;
}

// extracts the file name from a given path
const char *PathToFileName(const char *str)
{
    const char *p = strrchr(str, '/');
    return p ? p+1 : str;
}

void MakeSlashTerminated(std::string& s)
{
    if(s.length() && s[s.length() - 1] != '/')
        s += '/';
}

bool IsDirectory(const char *s)
{
#if PLATFORM == PLATFORM_WIN32
    DWORD dwFileAttr = GetFileAttributes(s);
    if(dwFileAttr == INVALID_FILE_ATTRIBUTES)
        return false;
    return !!(dwFileAttr & FILE_ATTRIBUTE_DIRECTORY);
#else
    if ( access( s, 0 ) == 0 )
    {
        struct stat status;
        stat( s, &status );
        return status.st_mode & S_IFDIR;
    }
    return false;
#endif
}

// from http://board.byuu.org/viewtopic.php?f=10&t=1089&start=15
bool WildcardMatch(const char *str, const char *pattern)
{
    const char *cp = 0, *mp = 0;
    while(*str && *pattern != '*')
    {
        if(*pattern != *str && *pattern != '?')
            return false;
        pattern++, str++;
    }

    while(*str)
    {
        if(*pattern == '*')
        {
            if(!*++pattern)
                return 1;
            mp = pattern;
            cp = str + 1;
        }
        else if(*pattern == *str || *pattern == '?')
        {
            ++pattern;
            ++str;
        }
        else
        {
            pattern = mp;
            str = cp++;
        }
    }

    while(*pattern++ == '*');

    return !*pattern;
}

uint32 GetConsoleWidth(void)
{
#if PLATFORM == PLATFORM_WIN32
    HANDLE hOut;
    CONSOLE_SCREEN_BUFFER_INFO SBInfo;
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(hOut,
        &SBInfo);
    return SBInfo.dwSize.X;
#else
    struct winsize ws;
    if (ioctl(0,TIOCGWINSZ,&ws))
        return 80; // the standard, because we don't know any better
    return ws.ws_col;
#endif
}

LVPA_NAMESPACE_END
