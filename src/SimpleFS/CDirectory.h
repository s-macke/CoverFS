#ifndef CDIRECTORY_H
#define CDIRECTORY_H

#include "CSimpleFS.h"
#include <mutex>
#include <cassert>
#include <cstdint>
#include <functional>

enum class FOREACHENTRYRET {OK, QUIT, WRITEANDQUIT};

// ----------------------------------------------------------

class DIRENTRY
{
public:
    explicit DIRENTRY(const std::string &_name="", int32_t _id=CFragmentDesc::INVALIDID) : id(_id)
    {
        memset(name, 0, 64+32);
        memset(dummy, 0, 16+8);
        _name.copy(name, sizeof name);
    }

    int32_t id;
    char name[64+32]{};
    char dummy[16+12]{};
};


class CDirectory
{
public:
    CDirectory(INODEPTR node, SimpleFilesystem &_fs);
    int GetID() {return dirnode->id;}
    void Create();
    void ForEachEntry(std::function<FOREACHENTRYRET(DIRENTRY &de)> f);
    void ForEachEntryNonBlocking(std::function<FOREACHENTRYRET(DIRENTRY &de)> f);
    int CreateDirectory(const std::string &name);
    int CreateFile(const std::string &name);
    void AddEntry(const DIRENTRY &de);
    void RemoveEntry(const std::string &name, DIRENTRY &e);
    void Find(const std::string &s, DIRENTRY &e);
    bool IsEmpty();
    void List();
    
    INODEPTR dirnode;

private:
    SimpleFilesystem &fs;
    int blocksize;
};


#endif
