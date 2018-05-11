#ifndef INODE_H
#define INODE_H

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>

#include"../CFilesystem.h"

class CSimpleFilesystem;

class CSimpleFSInode : public CInode
{
    friend class CSimpleFSDirectory;
    friend class CSimpleFSInternalDirectoryIterator;
    friend class CSimpleFSDirectoryIterator;
    friend class CSimpleFilesystem;

public:
    explicit CSimpleFSInode(CSimpleFilesystem &_fs) : id(-4), parentid(-4), size(0), nlinks(1), type(INODETYPE::undefined), fs(_fs) {}

    virtual ~CSimpleFSInode();

    int64_t Read(int8_t *d, int64_t ofs, int64_t size) override;
    void Write(const int8_t *d, int64_t ofs, int64_t size) override;
    void Truncate(int64_t size, bool dozero) override;

    int64_t GetSize() override;
    INODETYPE GetType() override;
    int32_t GetId() override;

private:

    std::mutex& GetMutex() { return mtx; } // for lock_guard
    void Lock();
    void Unlock();

    // non-blocking read and write
    int64_t ReadInternal(int8_t *d, int64_t ofs, int64_t size);
    void WriteInternal(const int8_t *d, int64_t ofs, int64_t size);

    int32_t id;
    int32_t parentid;
    int64_t size;
    std::atomic<int> nlinks;
    INODETYPE type;
    std::string name;
    std::vector<int> fragments;

    std::mutex mtx;
    CSimpleFilesystem &fs;

};
using CSimpleFSInodePtr = std::shared_ptr<CSimpleFSInode>;

#endif
