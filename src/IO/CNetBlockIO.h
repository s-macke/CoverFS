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

    void Read(int blockidx, int n, int8_t* d);
    void Write(int blockidx, int n, int8_t* d);
    int64_t GetFilesize() override;
    int64_t GetWriteCache() override;
    void GetInfo();
    void Close();

private:
    boost::asio::io_service io_service;
    ssl::context ctx;
    ssl_socket sctrl;
    ssl_socket sdata;
    std::atomic_int cmdid;
    std::thread iothread;
    std::unique_ptr<boost::asio::io_service::work> work;
    std::unique_ptr<CNetReadWriteBuffer> rbbufctrl;
    std::unique_ptr<CNetReadWriteBuffer> rbbufdata;
};

#endif
