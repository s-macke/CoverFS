#ifndef CBLOCKIO_H
#define CBLOCKIO_H

#include<vector>
#include<memory>
#include<mutex>
#include<stdint.h>
#include<stdlib.h>


class CAbstractBlockIO;

class CAbstractBlockIO
{
public:
    CAbstractBlockIO(int _blocksize);
    virtual void Read(const int blockidx, const int n, int8_t* d) = 0;
    virtual void Write(const int blockidx, const int n, int8_t* d) = 0;
    virtual int64_t GetFilesize() = 0;

public:
    unsigned int blocksize;
};


class CRAMBlockIO : public CAbstractBlockIO
{
public:
    CRAMBlockIO(int _blocksize);
    void Read(const int blockidx, const int n, int8_t* d);
    void Write(const int blockidx, const int n, int8_t* d);
    int64_t GetFilesize();

private:
    std::vector<int8_t> data;
    std::mutex mutex;
};

#endif
