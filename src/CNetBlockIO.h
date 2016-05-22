#ifndef CNETIO_H
#define CNETIO_H

#include "CBlockIO.h"

#include <thread>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

#include <mutex>
#include <atomic>


class CNetReadWriteBuffer;

class CNetBlockIO : public CAbstractBlockIO
{
public:
    CNetBlockIO(int _blocksize, const std::string &host, const std::string &port);
    ~CNetBlockIO();

    void Read(const int blockidx, const int n, int8_t* d);
    void Write(const int blockidx, const int n, int8_t* d);
    int64_t GetFilesize();
    void GetInfo();

private:
    boost::asio::io_service io_service;
    ssl::context ctx;
    ssl_socket s;
    std::atomic_int cmdid;
    std::thread iothread;
    CNetReadWriteBuffer *rbbuf;
};

#endif
