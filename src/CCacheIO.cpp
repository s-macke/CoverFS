#include "CCacheIO.h"

// -----------------------------------------------------------------

CBlock::CBlock(CAbstractBlockIO &_bio, CEncrypt &_enc, int _blockidx, int8_t *_buf) : dirty(false), blockidx(_blockidx), bio(_bio), enc(_enc), buf(_buf), count(0)
{
}

void CBlock::Dirty()
{
    dirty = true;
}

// TODO: maybe provide an extra class with special read and write access [] and lock_guards and so on.

int8_t* CBlock::GetBuf()
{
    mutex.lock();
    count++;
    enc.Decrypt(blockidx, buf);
    return buf;
}

void CBlock::ReleaseBuf()
{
    enc.Encrypt(blockidx, buf);
    mutex.unlock();
}

// -----------------------------------------------------------------

CCacheIO::CCacheIO(CAbstractBlockIO &_bio, CEncrypt &_enc) : bio(_bio), enc(_enc)
{
    blocksize = bio.blocksize;
}

CBLOCKPTR CCacheIO::GetBlock(const int blockidx, bool read)
{
    cachemtx.lock();
    auto cacheblock = cache.find(blockidx);
    if (cacheblock != cache.end())
    {
        cachemtx.unlock();
        return cacheblock->second;
    }
    int8_t *buf = new int8_t[blocksize];
    CBLOCKPTR block(new CBlock(bio, enc, blockidx, buf));
    cache[blockidx] = block;
    block->mutex.lock(); // don't use GetBuf because of the encryption
    cachemtx.unlock();
    if (read) bio.Read(blockidx, 1, buf);
    block->mutex.unlock();

    return block;
}

void CCacheIO::BlockReadForce(const int blockidx, const int n)
{
    if (n <= 0) return;
    int8_t *buf = new int8_t[blocksize*n];
    bio.Read(blockidx, n, buf);
    for(int j=0; j<n; j++)
    {
        CBLOCKPTR block(new CBlock(bio, enc, blockidx+j, &buf[j*blocksize]));
        cache[blockidx+j] = block;
    }
}

void CCacheIO::CacheBlocks(const int blockidx, const int n)
{
    if (n <= 0) return;
    cachemtx.lock(); // Solution too easy. We might want to to aquire the blocks and block them via the mutex before we read the content
    int istart = 0;
    for(int i=0; i<n; i++)
    {
        auto cacheblock = cache.find(blockidx+i);
        if (cacheblock != cache.end())
        {
            int npart = i-istart;
            istart = i+1;
            BlockReadForce(blockidx+istart, npart);
        }
    }
    int npart = n-istart;
    BlockReadForce(blockidx+istart, npart);
    cachemtx.unlock();
}

size_t CCacheIO::GetFilesize()
{
    return bio.GetFilesize();
}

void CCacheIO::Sync()
{
    cachemtx.lock();
    for(auto it = cache.begin(); it != cache.end(); ++it) 
    {
        CBLOCKPTR block = it->second;
        if (block->mutex.try_lock())
        {
            if (block->dirty) // Bad solution for dirty flag, but otherwise valgrind complains. Use a list instead of flag
            {
                bio.Write(block->blockidx, 1, block->buf);
                block->dirty = false;
            }
            block->mutex.unlock();
        }
    }
    cachemtx.unlock();
}
