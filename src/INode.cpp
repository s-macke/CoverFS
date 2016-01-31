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
    return fs.Read(*this, d, ofs, size);
}

void INODE::Write(const int8_t *d, int64_t ofs, int64_t size)
{
    fs.Write(*this, d, ofs, size);
}

void INODE::Truncate(int64_t size)
{
    fs.Truncate(*this, size, true);
}

void INODE::Remove()
{
    fs.Remove(*this);
}

void INODE::Print()
{
}
