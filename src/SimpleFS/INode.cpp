#include "CSimpleFS.h"

#include "INode.h"

void INODE::Lock()
{
    mtx.lock();
}

void INODE::Unlock()
{
    mtx.unlock();
}

int64_t INODE::Read(int8_t *d, int64_t ofs, int64_t size)
{
    std::lock_guard<std::mutex> lock(mtx);
    return fs.Read(*this, d, ofs, size);
}

void INODE::Write(const int8_t *d, int64_t ofs, int64_t size)
{
    std::lock_guard<std::mutex> lock(mtx);
    fs.Write(*this, d, ofs, size);
}

void INODE::Truncate(int64_t size, bool dozero)
{
    std::lock_guard<std::mutex> lock(mtx);
    fs.Truncate(*this, size, dozero);
}

void INODE::Remove()
{
    std::lock_guard<std::mutex> lock(mtx);
    fs.Remove(*this);
}

// non-blocking read and write
void INODE::WriteInternal(const int8_t *d, int64_t ofs, int64_t size)
{
    fs.Write(*this, d, ofs, size);
}

int64_t INODE::ReadInternal(int8_t *d, int64_t ofs, int64_t size)
{
    return fs.Read(*this, d, ofs, size);
}

