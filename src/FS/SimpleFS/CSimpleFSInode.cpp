#include "CSimpleFS.h"
#include "CSimpleFSInode.h"

CSimpleFSInode::~CSimpleFSInode() {
    fs.MaybeRemove(*this);
}

int64_t CSimpleFSInode::Read(int8_t *d, int64_t ofs, int64_t size)
{
    std::lock_guard<std::mutex> lock(mtx);
    return fs.Read(*this, d, ofs, size);
}

void CSimpleFSInode::Write(const int8_t *d, int64_t ofs, int64_t size)
{
    std::lock_guard<std::mutex> lock(mtx);
    fs.Write(*this, d, ofs, size);
}

void CSimpleFSInode::Truncate(int64_t size, bool dozero)
{
    std::lock_guard<std::mutex> lock(mtx);
    fs.Truncate(*this, size, dozero);
}

// non-blocking read and write
void CSimpleFSInode::WriteInternal(const int8_t *d, int64_t ofs, int64_t size)
{
    fs.Write(*this, d, ofs, size);
}

int64_t CSimpleFSInode::ReadInternal(int8_t *d, int64_t ofs, int64_t size)
{
    return fs.Read(*this, d, ofs, size);
}

int64_t CSimpleFSInode::GetSize()
{
    std::lock_guard<std::mutex> lock(mtx);
    return size;
}

INODETYPE CSimpleFSInode::GetType()
{
    std::lock_guard<std::mutex> lock(mtx);
    return type;
}

int32_t CSimpleFSInode::GetId()
{
    std::lock_guard<std::mutex> lock(mtx);
    return id;
}


