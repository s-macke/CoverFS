#ifndef INODE_H
#define INODE_H

#include <memory>
#include <vector>
#include <mutex>

class SimpleFilesystem;

enum class INODETYPE : int32_t {unknown=0, dir=1, file=2, free=-1};

class INODE
{
    friend class CDirectory;

public:
    int32_t id;
    int32_t parentid;
    int64_t size;
    INODETYPE type;
    std::string name;
    std::vector<int> fragments;
    INODE(SimpleFilesystem &_fs) : id(-4), parentid(-4), size(0), type(INODETYPE::unknown), fs(_fs) {}
    std::mutex& GetMutex() { return mtx; } // for lock_guard
    void Lock();
    void Unlock();

    int64_t Read(int8_t *d, int64_t ofs, int64_t size);
    void Write(const int8_t *d, int64_t ofs, int64_t size);
    void Truncate(int64_t size, bool dozero=true);
    void Remove();
    void Print();

private:

    // non-blocking read and write
    int64_t ReadInternal(int8_t *d, int64_t ofs, int64_t size);
    void WriteInternal(const int8_t *d, int64_t ofs, int64_t size);

    std::mutex mtx;
    SimpleFilesystem &fs;

};
using INODEPTR = std::shared_ptr<INODE>;

#endif
