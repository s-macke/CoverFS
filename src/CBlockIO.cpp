#include "CBlockIO.h"
#include <string.h>

CAbstractBlockIO::CAbstractBlockIO(int _blocksize) : blocksize(_blocksize) {}

// -----------------------------------------------------------------


CRAMBlockIO::CRAMBlockIO(int _blocksize) : CAbstractBlockIO(_blocksize)
{
    data.assign(blocksize*3, 0xFF);
}

size_t CRAMBlockIO::GetFilesize()
{
    return data.size();
}

void CRAMBlockIO::Read(const int blockidx, const int n, int8_t *d)
{
    uint64_t newsize = (blockidx+n)*blocksize;
    if (newsize > data.size())
    {
        data.resize((newsize*3)/2, 0xFF);
    }
    memcpy(d, &data[blockidx*blocksize], n*blocksize);
}

void CRAMBlockIO::Write(const int blockidx, const int n, int8_t* d)
{
    uint64_t newsize = (blockidx+n)*blocksize;
    if (newsize > data.size())
    {
        data.resize((newsize*3)/2, 0xFF);
    }
    memcpy(&data[blockidx*blocksize], d, n*blocksize);
}

