#include "CCacheIO.h"

// -----------------------------------------------------------------

CBlock::CBlock(CAbstractBlockIO &_bio, CEncrypt &_enc, int _blockidx, int8_t *_buf) : dirty(false), blockidx(_blockidx), bio(_bio), enc(_enc), buf(_buf) 
{
}

void CBlock::Dirty()
{
    // TODO: assert if mutex is locked. Cannot do, because std::mutex doesn't provide such a function 
    dirty = true;
}

// TODO: maybe provide an extra class with special read and write access [] and lock_guards and so on.

int8_t* CBlock::GetBuf()
{
    mutex.lock();
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
    auto cacheblock = cache.find(blockidx);
    if (cacheblock != cache.end())
    {
        return cacheblock->second;
    }
    int8_t *buf = new int8_t[blocksize];
    if (read) bio.Read(blockidx, 1, buf);
    
    CBLOCKPTR block(new CBlock(bio, enc, blockidx, buf));
    cache[blockidx] = block;
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
}

size_t CCacheIO::GetFilesize()
{
    return bio.GetFilesize();
}

void CCacheIO::Sync()
{
    for(auto it = cache.begin(); it != cache.end(); ++it) 
    {
        CBLOCKPTR block = it->second;

        if (block->dirty)
        {
        if (block->mutex.try_lock())
        {
            //printf("Sync block %i\n", block->blockidx);                        
            bio.Write(block->blockidx, 1, block->buf);
            block->mutex.unlock();
            block->dirty = false;
        } /*else
        {
             fprintf(stderr, "Sync of locked data\n");
             exit(1);
        }*/
        }
    }
}
