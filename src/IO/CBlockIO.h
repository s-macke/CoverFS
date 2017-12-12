#ifndef CBLOCKIO_H
#define CBLOCKIO_H

#include<vector>
#include<memory>
#include<mutex>
#include<cstdint>
#include<cstdlib>


class CAbstractBlockIO;

class CAbstractBlockIO
{
public:
    explicit CAbstractBlockIO(int _blocksize);
    virtual void Read(int blockidx, int n, int8_t* d) = 0;
    virtual void Write(int blockidx, int n, int8_t* d) = 0;
    virtual int64_t GetFilesize() = 0;
    virtual int64_t GetWriteCache();

public:
    unsigned int blocksize;
};


class CRAMBlockIO : public CAbstractBlockIO
{
public:
    explicit CRAMBlockIO(int _blocksize);
    void Read(int blockidx, int n, int8_t* d) override;
    void Write(int blockidx, int n, int8_t* d) override;
    int64_t GetFilesize() override;

private:
    std::vector<int8_t> data;
    std::mutex mutex;
};

#endif
