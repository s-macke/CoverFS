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
class CSimpleFSDirectory;

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

class CSimpleFSInternalDirectoryIterator
{

public:
    explicit CSimpleFSInternalDirectoryIterator(CSimpleFSDirectory &_directory);

    bool  HasNext();
    CDirectoryEntryOnDisk  Next();
    uint64_t GetOffset();
    CSimpleFSDirectory& GetDirectory();

private:
    CSimpleFSDirectory &directory;

    void GetNextBlock();

    std::vector<int8_t> buf;
    uint64_t ofs = 0;
    int64_t size = 0;
    uint64_t idx = 0;
    uint64_t nentriesperblock = 0;
};
using CSimpleFSInternalDirectoryIteratorPtr = std::unique_ptr<CSimpleFSInternalDirectoryIterator>;


class CSimpleFSDirectoryIterator : public CDirectoryIterator
{
public:

    explicit CSimpleFSDirectoryIterator(CSimpleFSInternalDirectoryIteratorPtr &&iterator);

    bool HasNext() override;
    CDirectoryEntry Next() override;

private:
    CSimpleFSInternalDirectoryIteratorPtr iterator;
    std::lock_guard<std::mutex> lock;
    CDirectoryEntry de;
};


class CSimpleFSDirectory : public CDirectory
{
    friend CSimpleFilesystem;
    friend CPrintCheckRepair;
    friend CSimpleFSInternalDirectoryIterator;
    friend CSimpleFSDirectoryIterator;

public:
    CSimpleFSDirectory(CSimpleFSInodePtr node, CSimpleFilesystem &_fs);

    CDirectoryIteratorPtr GetIterator() override;
    CSimpleFSInternalDirectoryIteratorPtr GetInternalIterator();

    int MakeDirectory(const std::string& name) override;
    int MakeFile(const std::string& name) override;

    int32_t GetId() override;
    void Remove() override;
    bool IsEmpty() override;
    void CreateEmptyBlock(int8_t* buf);

private:
    void Find(const std::string &s, CDirectoryEntryOnDisk &e);
    void RemoveEntry(const std::string &name, CDirectoryEntryOnDisk &e);
    void AddEntry(const CDirectoryEntryOnDisk &de);
    void Create();

    int GetID() {return dirnode->id;}
    void List();

    CSimpleFSInodePtr dirnode;
    CSimpleFilesystem &fs;
    int blocksize;
};
using CSimpleFSDirectoryPtr = std::shared_ptr<CSimpleFSDirectory>;


#endif
