#ifndef CCACHEIO_H
#define CCACHEIO_H

#include <map>
#include <vector>
#include <mutex>

#include "CBlockIO.h"
#include "CEncrypt.h"

// TODO: Use this structure for GetBuf and maybe do something with the dirty flag
class CBlockBuffer
{
public:
    CBlockBuffer(int8_t *_buf, std::mutex &mutex) : lock(mutex), buf(_buf) {};

    int8_t& operator[](std::size_t idx) { return buf[idx]; };
    const int8_t& operator[](std::size_t idx) const { return buf[idx]; };

private:
    std::lock_guard<std::mutex> lock;
    int8_t *buf;
};

class CBlock
{
    friend class CCacheIO;
public:
    CBlock(CAbstractBlockIO &_bio, CEncrypt &_enc, int _blockidx, int8_t *_buf); 
    int8_t* GetBuf();
    void ReleaseBuf();

    void Dirty();
    bool dirty;
    int blockidx;

private:
    std::mutex mutex;
    CAbstractBlockIO &bio;
    CEncrypt &enc;
    int8_t *buf;
};

using CBLOCKPTR = std::shared_ptr<CBlock>;

class CCacheIO
{
public:
    CCacheIO(CAbstractBlockIO &bio, CEncrypt &_enc);
    CBLOCKPTR GetBlock(const int blockidx, bool write=false);
    CBLOCKPTR GetWriteBlock(const int blockidx);
    void CacheBlocks(const int blockidx, const int n);
    void BlockReadForce(const int blockidx, const int n);

    size_t GetFilesize();
    void Sync();
    int blocksize;

private:
    CAbstractBlockIO &bio;
    CEncrypt &enc;
    std::map<int, CBLOCKPTR> cache;
};

#endif
