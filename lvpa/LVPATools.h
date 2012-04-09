#ifndef _TOOLS_H
#define _TOOLS_H

#include <deque>
#include <string>

#include "LVPACommon.h"

LVPA_NAMESPACE_START

typedef std::deque<std::string> StringList;

// shared with ttvfs
std::string stringToUpper(const std::string& s);
std::string stringToLower(const std::string& s);
void makeUppercase(std::string& s);
void makeLowercase(std::string& s);
bool IsDirectory(const char *);
bool CreateDir(const char*);
bool CreateDirRec(const char*);
void GetFileList(const char *, StringList& files);
void GetDirList(const char *, StringList& dirs, bool recursive = false);
void GetFileListRecursive(std::string dir, StringList& files, bool withQueriedDir = false);
uint32 GetFileSize(const char*);
std::string FixSlashes(const std::string& s);
const char *PathToFileName(const char *str);
void MakeSlashTerminated(std::string& s);
uint32 GetConsoleWidth(void);
std::string GenerateTempFileName(const std::string& fn);
bool FileIsWriteable(const std::string& fn);

// for lvpak
bool WildcardMatch(const char *str, const char *pattern);
uint32 GetConsoleWidth(void);

// for lzham
uint32 ilog2(uint32);


template <class T> void StrSplit(const std::string &src, const std::string &sep, T& container, bool keepEmpty = false)
{
    std::string s;
    for (std::string::const_iterator i = src.begin(); i != src.end(); i++)
    {
        if (sep.find(*i) != std::string::npos)
        {
            if (keepEmpty || s.length())
                container.push_back(s);
            s = "";
        }
        else
        {
            s += *i;
        }
    }
    if (keepEmpty || s.length())
        container.push_back(s);
}

LVPA_NAMESPACE_END

#endif
