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
    void Execute(CWriteRingBuffer *object);
    void Push(int8_t *d, int n);

    ssl_socket &socket;

    std::vector<int8_t> buf;
    unsigned int pushidx;
    unsigned int popidx;
    std::atomic_uint size;

    std::condition_variable cond;
    std::mutex condmtx;
    std::condition_variable pushblockcond;
    std::mutex pushblockcondmtx;
    std::mutex pushmtx;
    std::thread thread;
};

#endif
