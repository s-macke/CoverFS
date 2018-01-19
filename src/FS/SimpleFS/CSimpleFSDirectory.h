#ifndef CDIRECTORY_H
#define CDIRECTORY_H

#include <mutex>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>

#include"../CFilesystem.h"
#include"CFragment.h"

class CPrintCheckRepair;

// TODO This shouldn't be public, but inside the .cpp file
class CDirectoryEntryOnDisk {
public:
    explicit CDirectoryEntryOnDisk(const std::string &_name="", int32_t _id=CFragmentDesc::INVALIDID) : id(_id)
    {
        memset(name, 0, 64+32);
        memset(dummy, 0, 16+8);
        _name.copy(name, sizeof name);
    }

    char name[64+32]{};
    char dummy[16+12]{};
    int32_t id;
};


class CSimpleFSDirectory : public CDirectory
{
    friend CSimpleFilesystem;
    friend CPrintCheckRepair;

public:
    CSimpleFSDirectory(CSimpleFSInodePtr node, CSimpleFilesystem &_fs);

    void ForEachEntry(std::function<void(CDirectoryEntry &de)> f) override;
    void ForEachEntryNonBlocking(std::function<void(CDirectoryEntry &de)> f) override;

    int MakeDirectory(const std::string& name) override;
    int MakeFile(const std::string& name) override;

    int32_t GetId() override;
    void Remove() override;
    bool IsEmpty() override;

private:
    void Find(const std::string &s, CDirectoryEntryOnDisk &e);
    void RemoveEntry(const std::string &name, CDirectoryEntryOnDisk &e);
    void AddEntry(const CDirectoryEntryOnDisk &de);
    void ForEachEntryNonBlockingIntern(std::function<FOREACHENTRYRET(CDirectoryEntryOnDisk &de)> f);
    void ForEachEntryIntern(std::function<FOREACHENTRYRET(CDirectoryEntryOnDisk &de)> f);
    void Create();
    int GetID() {return dirnode->id;}
    void List();

    CSimpleFSInodePtr dirnode;
    CSimpleFilesystem &fs;
    int blocksize;
};
using CSimpleFSDirectoryPtr = std::shared_ptr<CSimpleFSDirectory>;


#endif
