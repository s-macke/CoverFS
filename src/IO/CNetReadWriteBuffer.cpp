#include"Logger.h"
#include "CNetReadWriteBuffer.h"


CNetReadWriteBuffer::CNetReadWriteBuffer(ssl_socket &s) : socket(s)
{
    // prepare write ring buffer
    buf.assign(1024*1024, 0);
    pushidx = 0;
    popidx = 0;
    bufsize = 0;

    // Start the async read loop
    AsyncRead();
}

CNetReadWriteBuffer::~CNetReadWriteBuffer()
{
    LOG(LogLevel::DEBUG) << "CNetReadWriteBuffer: destruct";
    Sync();
    LOG(LogLevel::DEBUG) << "CNetReadWriteBuffer: destruct done";
}

// --------------------------------------------------------

void CNetReadWriteBuffer::Sync()
{
    {
        std::lock_guard<std::mutex> lock(readidmapmtx);
        unsigned long n = readidmap.size();
        if (n != 0)
        {
            LOG(LogLevel::WARN) << "Read queue not empty";
        }
    }
    while(true)
    {
        size_t n = bufsize.load();
        if (n == 0)
        {
            LOG(LogLevel::DEBUG) << "CNetReadWriteBuffer: sync done";
            return;
        }
        LOG(LogLevel::INFO) << "write cache: emptying cache. Still "<< n << "bytes to go.";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

std::future<void> CNetReadWriteBuffer::Read(int32_t id, int8_t *buf, int32_t size)
{
    CReadBufferDesc rbi(buf, size);
    std::lock_guard<std::mutex> lock(readidmapmtx);
    readidmap[id] = std::move(rbi);
    return readidmap[id].promise.get_future();
}


void CNetReadWriteBuffer::AsyncRead2(int32_t size, int32_t id)
{
    static CReadBufferDesc rbi;
    std::lock_guard<std::mutex> lock(readidmapmtx);
    auto it = readidmap.find(id);
    assert(it != readidmap.end());
    rbi = std::move(it->second);
    readidmap.erase(it);

    boost::asio::async_read(
    socket,
    boost::asio::buffer(rbi.buf, rbi.size),
    [this](const boost::system::error_code& ec, std::size_t readbytes)
    {
        assert(readbytes == rbi.size);
        rbi.promise.set_value();
        AsyncRead();
    });
}

void CNetReadWriteBuffer::AsyncRead()
{
    static int32_t data[2];
    boost::asio::async_read(
    socket,
    boost::asio::buffer(data, 8),
    [this](const boost::system::error_code& ec, std::size_t readbytes)
    {
        AsyncRead2(data[0]-8, data[1]);
    });
}

// --------------------------------------------------------

void CNetReadWriteBuffer::Write(int32_t id, int8_t *d, int n)
{
    std::lock_guard<std::mutex> lock(writemtx);
    int32_t data[2];
    data[0] = n+8; // total length of packet
    data[1] = id;  // unique id of packet
    Push((int8_t*)data, 8);
    Push(d, n);
    AsyncWrite();
}

void CNetReadWriteBuffer::Push(int8_t *d, int n)
{
    //printf("Push %i bytes\n", n);
    for(int i=0; i<n; i++)
    {
        // ringbuffer full. Block all further evaluations
        if (bufsize.load() > buf.size()-2)
        {
            LOG(LogLevel::DEEP) << "Ringbuffer full: blocking";
            AsyncWrite();
            std::unique_lock<std::mutex> lock(condmtx);
            while(bufsize.load() > buf.size()-2)
            {
                cond.wait_for(lock, std::chrono::milliseconds(500));
            }
        }
        buf[pushidx] = d[i];
        pushidx++;
        bufsize.fetch_add(1);
        if (pushidx >= buf.size()) pushidx -= buf.size();
    }
}

void CNetReadWriteBuffer::AsyncWrite()
{
    wpmutex.lock();
    bool wip = write_in_progress.test_and_set();
    wpmutex.unlock();

    if (wip) return; // return if it already runs
    //if (bufsize.load() > 0) printf("write size=%i\n", bufsize.load());
    size_t sendsize = std::min(buf.size()-popidx, bufsize.load());
    boost::asio::async_write(
    socket,
    boost::asio::buffer(&(buf[popidx]), sendsize),
    [this](const boost::system::error_code& ec, std::size_t writtenbytes)
    {
        //std::unique_lock<std::mutex> lock(condmtx);
        //printf("written bytes %li\n", writtenbytes);
        popidx += writtenbytes;
        bufsize.fetch_sub(writtenbytes);
        if (popidx >= buf.size()) popidx -= buf.size();
        cond.notify_one();
        wpmutex.lock();
        write_in_progress.clear();
        wpmutex.unlock();
        if (bufsize.load() > 0) AsyncWrite();
    });
}

int64_t CNetReadWriteBuffer::GetBytesInCache()
{
    return bufsize.load();
}
