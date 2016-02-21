#include "CCacheIO.h"
#include <assert.h>

// -----------------------------------------------------------------

CBlock::CBlock(CCacheIO &_cio, CEncrypt &_enc, int _blockidx, int8_t *_buf) : nextdirtyidx(-1), blockidx(_blockidx), cio(_cio), enc(_enc), buf(_buf), count(0)
{}


int8_t* CBlock::GetBufRead()
{
    mutex.lock();
    count++;
    enc.Decrypt(blockidx, buf);
    return buf;
}

int8_t* CBlock::GetBufReadWrite()
{
    int8_t* buf = GetBufRead();
    if (nextdirtyidx == -1)
    {
        nextdirtyidx = cio.lastdirtyidx.exchange(blockidx, std::memory_order_relaxed);
        cio.ndirty++;
    }
    return buf;
}

void CBlock::ReleaseBuf()
{
    enc.Encrypt(blockidx, buf);
    mutex.unlock();
}

// -----------------------------------------------------------------

CCacheIO::CCacheIO(CAbstractBlockIO &_bio, CEncrypt &_enc) : bio(_bio), enc(_enc), ndirty(0), lastdirtyidx(-1), terminatesyncthread(false)
{
    blocksize = bio.blocksize;
    syncthread = std::thread(&CCacheIO::Async_Sync, this);
}

CCacheIO::~CCacheIO()
{
    terminatesyncthread.store(true);
    Sync();
    syncthread.join();
    assert(ndirty.load() == 0);

    cachemtx.lock();
    for(auto iter = cache.begin(); iter != cache.end();)
    {
        CBLOCKPTR block = iter->second;
        if (block.use_count() != 2)
        {
            printf("Warning: Block %i still in use\n", block->blockidx);
            iter++;
            continue;
        }
        if (!block->mutex.try_lock())
        {
            printf("Error: Locking block %i failed\n", block->blockidx);
            iter++;
            continue;
        }
        iter = cache.erase(iter);
        delete[] block->buf;
        block->mutex.unlock();
    }
    if (!cache.empty())
    {
        printf("Warning: Cache not empty\n");
    }
    cachemtx.unlock();
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
    CBLOCKPTR block(new CBlock(*this, enc, blockidx, buf));
    cache[blockidx] = block;
    block->mutex.lock();
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
    cachemtx.lock();
    for(int i=0; i<n; i++)
    {
        auto cacheblock = cache.find(blockidx+i);
        assert(cacheblock != cache.end()); // block created in CacheBlocks
        CBLOCKPTR block = cacheblock->second;
        memcpy(block->buf, &buf[i*blocksize], blocksize);
        block->mutex.unlock();
    }
    cachemtx.unlock();
    delete[] buf;

}

void CCacheIO::CacheBlocks(const int blockidx, const int n)
{
    if (n <= 0) return;
    cachemtx.lock();
    int istart = 0;
    for(int i=0; i<n; i++)
    {
        auto cacheblock = cache.find(blockidx+i);
        if (cacheblock != cache.end())
        {
            int npart = i-istart;
            istart = i+1;
            cachemtx.unlock();
            BlockReadForce(blockidx+istart, npart);
            cachemtx.lock();
        } else
        {
            int8_t *buf = new int8_t[blocksize];
            CBLOCKPTR block(new CBlock(*this, enc, blockidx+i, buf));
            cache[blockidx+i] = block;
            block->mutex.lock();
        }
    }
    int npart = n-istart;
    cachemtx.unlock();
    BlockReadForce(blockidx+istart, npart);
}

size_t CCacheIO::GetFilesize()
{
    return bio.GetFilesize();
}

void CCacheIO::Async_Sync()
{
    std::unique_lock<std::mutex> lock(async_sync_mutex);
    for(;;)
    {
        while (ndirty.load() == 0)
        {
            if (terminatesyncthread.load()) return;
            async_sync_cond.wait(lock);
        }

        int nextblockidx = lastdirtyidx.exchange(-1, std::memory_order_relaxed);
        while(nextblockidx != -1)
        {
            cachemtx.lock();
            CBLOCKPTR block = cache.find(nextblockidx)->second;
            block->mutex.lock(); // TODO trylock and put back on the list
            cachemtx.unlock();
            nextblockidx = block->nextdirtyidx;
            bio.Write(block->blockidx, 1, block->buf);
            block->nextdirtyidx = -1;
            ndirty--;
            block->mutex.unlock();
        }
    }
}


void CCacheIO::Sync()
{
    std::unique_lock<std::mutex> lock(async_sync_mutex);
    async_sync_cond.notify_one();
}
