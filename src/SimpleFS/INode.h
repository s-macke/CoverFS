#ifndef INODE_H
#define INODE_H

#include <memory>
#include <vector>
#include <mutex>

class SimpleFilesystem;

enum class INODETYPE : int32_t {undefined=0, dir=1, file=2, special=3};

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

    explicit INODE(SimpleFilesystem &_fs) : id(-4), parentid(-4), size(0), type(INODETYPE::undefined), fs(_fs) {}
    std::mutex& GetMutex() { return mtx; } // for lock_guard
    void Lock();
    void Unlock();

    int64_t Read(int8_t *d, int64_t ofs, int64_t size);
    void Write(const int8_t *d, int64_t ofs, int64_t size);
    void Truncate(int64_t size, bool dozero=true);
    void Remove();

private:

    // non-blocking read and write
    int64_t ReadInternal(int8_t *d, int64_t ofs, int64_t size);
    void WriteInternal(const int8_t *d, int64_t ofs, int64_t size);

    std::mutex mtx;
    SimpleFilesystem &fs;

};
using INODEPTR = std::shared_ptr<INODE>;

#endif
