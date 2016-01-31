#ifndef CBLOCKIO_H
#define CBLOCKIO_H

#include<vector>
#include<memory>
#include<stdint.h>
#include<stdlib.h>


class CAbstractBlockIO;

class CAbstractBlockIO
{
public:
    CAbstractBlockIO(int _blocksize);
    virtual void Read(const int blockidx, int8_t* d) = 0;
    virtual void Write(const int blockidx, int8_t* d) = 0;
    virtual size_t GetFilesize() = 0;

public:
    unsigned int blocksize;
};


class CBlockIO : public CAbstractBlockIO
{
public:
    CBlockIO(int _blocksize);
    void Read(const int blockidx, int8_t* d);
    void Write(const int blockidx, int8_t* d);
    size_t GetFilesize();

private:
    size_t filesize;
    std::vector<int8_t> data;
};

#endif
