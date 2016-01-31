#include "CBlockIO.h"

CAbstractBlockIO::CAbstractBlockIO(int _blocksize) : blocksize(_blocksize) {}

// -----------------------------------------------------------------


CBlockIO::CBlockIO(int _blocksize) : CAbstractBlockIO(_blocksize)
{
    filesize = 1024*1024*100;
    data.assign(filesize, 0xFF);
}

size_t CBlockIO::GetFilesize()
{
    return filesize;
}

void CBlockIO::Read(const int blockidx, int8_t *d)
{
    for(unsigned int i=0; i<blocksize; i++) d[i]  = data[blockidx*blocksize+i];
}

void CBlockIO::Write(const int blockidx, int8_t* d)
{
    for(unsigned int i=0; i<blocksize; i++) data[blockidx*blocksize+i] = d[i];
}

