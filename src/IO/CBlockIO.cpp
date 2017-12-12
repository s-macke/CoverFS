#include "CBlockIO.h"
#include <cstring>

CAbstractBlockIO::CAbstractBlockIO(int _blocksize) : blocksize(_blocksize) {}
int64_t CAbstractBlockIO::GetWriteCache() { return 0; }

// -----------------------------------------------------------------

CRAMBlockIO::CRAMBlockIO(int _blocksize) : CAbstractBlockIO(_blocksize)
{
    data.assign(blocksize*3, 0xFF);
}

int64_t CRAMBlockIO::GetFilesize()
{
    std::lock_guard<std::mutex> lock(mutex);
    return data.size();
}


void CRAMBlockIO::Read(const int blockidx, const int n, int8_t *d)
{
    uint64_t newsize = (blockidx+n)*blocksize;
    std::lock_guard<std::mutex> lock(mutex);
    if (newsize > data.size())
    {
        data.resize((newsize*3)/2, 0xFF);
    }
    memcpy(d, &data[blockidx*blocksize], n*blocksize);
}

void CRAMBlockIO::Write(const int blockidx, const int n, int8_t* d)
{
    uint64_t newsize = (blockidx+n)*blocksize;
    std::lock_guard<std::mutex> lock(mutex);
    if (newsize > data.size())
    {
        data.resize((newsize*3)/2, 0xFF);
    }
    memcpy(&data[blockidx*blocksize], d, n*blocksize);
}
