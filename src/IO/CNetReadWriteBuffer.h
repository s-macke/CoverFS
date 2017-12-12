#ifndef CWRITERINGBUFFER_H
#define CWRITERINGBUFFER_H

#include<condition_variable>
#include<thread>
#include<future>
#include<atomic>
#include<vector>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

class CReadBufferDesc
{
    public:
    explicit CReadBufferDesc(int8_t *_buf= nullptr, size_t _size=0) : buf(_buf), size(_size) {}
    int8_t *buf;               // which buffer to fill
    size_t size;              // size of buffer
    std::promise<void> promise; // which task to notify
};

class CNetReadWriteBuffer
{
    public:
    explicit CNetReadWriteBuffer(ssl_socket &s);
    ~CNetReadWriteBuffer();
    void Write(int32_t id, int8_t *d, int n);
    std::future<void> Read(int32_t id, int8_t *buf, int32_t size);
    void Sync();
    int64_t GetBytesInCache();

    private:

    // for reading
    void AsyncRead();
    void AsyncRead2(int32_t size, int32_t id);
    std::mutex readidmapmtx;
    std::map<int32_t, CReadBufferDesc> readidmap;

    // for writing
    void Push(int8_t *d, int n);
    void AsyncWrite();

    // write buffer
    std::vector<int8_t> buf;
    unsigned int pushidx;
    unsigned int popidx;
    std::atomic_size_t bufsize;

    std::condition_variable cond;
    std::mutex condmtx;

    std::atomic_flag write_in_progress = ATOMIC_FLAG_INIT;
    std::mutex wpmutex;

    std::mutex writemtx;

    ssl_socket &socket;
};

#endif
