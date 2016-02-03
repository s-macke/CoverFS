#ifndef CNETIO_H
#define CNETIO_H

#include "CBlockIO.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

#include <mutex>

class CWriteRingBuffer;

class CNetBlockIO : public CAbstractBlockIO
{
public:
    CNetBlockIO(int _blocksize, const std::string &host, const std::string &port);
    void Read(const int blockidx, const int n, int8_t* d);
    void Write(const int blockidx, const int n, int8_t* d);
    size_t GetFilesize();

private:
    boost::asio::io_service io_service;
    ssl::context ctx;
    ssl_socket s;
    size_t filesize;
    std::mutex mtx;
    CWriteRingBuffer *writerb;
};

#endif
