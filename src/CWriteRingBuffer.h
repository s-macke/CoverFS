#ifndef CWRITERINGBUFFER_H
#define CWRITERINGBUFFER_H

#include<condition_variable>
#include<thread>
#include<atomic>
#include<vector>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

class CWriteRingBuffer
{
    public:
    CWriteRingBuffer(ssl_socket &s);
    void Push(int8_t *d, int n);
    void AsyncWrite();

    ssl_socket &socket;

    std::vector<int8_t> buf;
    unsigned int pushidx;
    unsigned int popidx;
    std::atomic_uint bufsize;

    std::condition_variable cond;
    std::mutex condmtx;

    std::atomic_flag write_in_progress = ATOMIC_FLAG_INIT;
    std::mutex wpmutex;

    std::mutex pushmtx;
};

#endif
