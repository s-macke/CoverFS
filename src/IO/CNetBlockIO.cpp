#include"Logger.h"
#include"CNetBlockIO.h"
#include"CNetReadWriteBuffer.h"

#include<iostream>
#include<memory>

using boost::asio::ip::tcp;

enum class COMMAND : int32_t {READ=0, WRITE=1, SIZE=2, CONTAINERINFO=3, CLOSE=4};

typedef struct
{
    int32_t cmd;
    int32_t dummy;
    int64_t offset;
    int64_t length;
    int64_t data;
} CommandDesc;

template <typename E>
constexpr auto to_underlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

bool verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx)
{
    // The verify callback can be used to check whether the certificate that is
    // being presented is valid for the peer. For example, RFC 2818 describes
    // the steps involved in doing this for HTTPS. Consult the OpenSSL
    // documentation for more details. Note that the callback is called once
    // for each certificate in the certificate chain, starting from the root
    // certificate authority.

    // In this example we will simply print the certificate's subject name.
    char subject_name[256];
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
    LOG(INFO) << "Verifying certificate: " << subject_name;

    return preverified;
}


CNetBlockIO::CNetBlockIO(int _blocksize, const std::string &host, const std::string &port)
: CAbstractBlockIO(_blocksize),
  ctx(io_service, ssl::context::sslv23),
  sctrl(io_service, ctx),
  sdata(io_service, ctx),
  cmdid(0)
{
    static_assert(sizeof(CommandDesc) == 32, "");
    LOG(INFO) << "Try to connect to " << host << ":" << port;

    ctx.set_verify_mode(boost::asio::ssl::context::verify_peer);
    try
    {
        ctx.load_verify_file("ssl/server.crt");
    } catch(boost::system::system_error &e)
    {
        LOG(ERROR) << "Error during loading or parsing of ssl/server.crt: " << e.what();
        throw std::exception();
    }

    sctrl.set_verify_mode(boost::asio::ssl::verify_peer);
    sctrl.set_verify_callback(verify_certificate);

    sdata.set_verify_mode(boost::asio::ssl::verify_peer);
    sdata.set_verify_callback(verify_certificate);

    tcp::resolver resolver(io_service);
    tcp::resolver::query q(host, port);
    boost::system::error_code ec;
    tcp::resolver::iterator iter = resolver.resolve(q, ec);
    if (ec)
    {
        LOG(ERROR) << "Cannot resolve host";
        throw std::exception();
    }
    LOG(INFO) << "Connect to " << host;

    boost::asio::connect(sctrl.lowest_layer(), iter, ec);
    if (ec)
    {
        LOG(ERROR) << "Cannot connect to server. (Control Stream)\n";
        throw std::exception();
    }
    sctrl.handshake(boost::asio::ssl::stream_base::client);

    boost::asio::connect(sdata.lowest_layer(), iter, ec);
    if (ec)
    {
        LOG(ERROR) << "Cannot connect to server. (Data Stream)";
        throw std::exception();
    }
    sdata.handshake(boost::asio::ssl::stream_base::client);

#ifdef __linux__
    int priority = 6;
    int ret = setsockopt(sctrl.lowest_layer().native(), SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
    if (ret != 0)
    {
        LOG(WARN) << "Cannot set socket priority";
    }
#endif

    rbbufctrl = std::make_unique<CNetReadWriteBuffer>(sctrl);
    rbbufdata = std::make_unique<CNetReadWriteBuffer>(sdata);

    iothread = std::thread([&](){
        work = std::make_unique<boost::asio::io_service::work>(io_service);
        io_service.run();
    });

    GetInfo();
}

CNetBlockIO::~CNetBlockIO()
{

    LOG(DEBUG) << "CNetBlockIO: Destruct";
    LOG(DEBUG) << "CNetBlockIO: Send Close command";
    Close();
    LOG(DEBUG) << "CNetBlockIO: Send Close command done";

    rbbufctrl->Sync();
    rbbufdata->Sync();

    work.reset();
    io_service.stop();
    iothread.join();

    LOG(DEBUG) << "CNetBlockIO: Destruct buffer";
    rbbufctrl.reset();
    rbbufdata.reset();
    LOG(DEBUG) << "CNetBlockIO: Destruct buffers done";

    LOG(DEBUG) << "CNetBlockIO: Destruct done";
}

int64_t CNetBlockIO::GetWriteCache()
{
    return rbbufctrl->GetBytesInCache() + rbbufdata->GetBytesInCache();
}

int64_t CNetBlockIO::GetFilesize()
{
    int64_t filesize;
    CommandDesc cmd{};
    int8_t data[8];
    int32_t id = cmdid.fetch_add(1);
    cmd.cmd = to_underlying(COMMAND::SIZE);
    std::future<void> fut = rbbufctrl->Read(id, data, 8);
    rbbufctrl->Write(id, (int8_t*)&cmd, 4);
    fut.get();
    memcpy(&filesize, data, sizeof(filesize)); // to prevent the aliasing warning

    return filesize;
}

void CNetBlockIO::GetInfo()
{
    CommandDesc cmd{};
    int8_t data[36];
    int32_t id = cmdid.fetch_add(1);
    cmd.cmd = to_underlying(COMMAND::CONTAINERINFO);
    std::future<void> fut = rbbufctrl->Read(id, data, 36);
    rbbufctrl->Write(id, (int8_t*)&cmd, 4);
    fut.get();
    LOG(INFO) << "Connected to '" << data << "'";
}


void CNetBlockIO::Close()
{
    CommandDesc cmd{};
    int8_t data[8];
    int32_t id = cmdid.fetch_add(1);
    cmd.cmd = to_underlying(COMMAND::CLOSE);
    std::future<void> futctrl = rbbufctrl->Read(id, data, 0);
    std::future<void> futdata = rbbufdata->Read(id, data, 0);
    rbbufctrl->Write(id, (int8_t*)&cmd, 4);
    rbbufdata->Write(id, (int8_t*)&cmd, 4);
    futctrl.get();
    futdata.get();
}


void CNetBlockIO::Read(const int blockidx, const int n, int8_t *d)
{
    CommandDesc cmd{};
    int32_t id = cmdid.fetch_add(1);
    cmd.cmd = to_underlying(COMMAND::READ);
    cmd.dummy = 0;
    cmd.offset = blockidx*blocksize;
    cmd.length = blocksize*n;
    //printf("read block %i\n", blockidx);
    std::future<void> fut = rbbufctrl->Read(id, d, blocksize*n);
    rbbufctrl->Write(id, (int8_t*)&cmd, 2*4+2*8);
    fut.get();
}

void CNetBlockIO::Write(const int blockidx, const int n, int8_t* d)
{
    int8_t buf[blocksize*n + 2*8 + 2*4];
    auto *cmd = (CommandDesc*)buf;
    int32_t id = cmdid.fetch_add(1);
    cmd->cmd = to_underlying(COMMAND::WRITE);
    cmd->dummy = 0;
    cmd->offset = blockidx*blocksize;
    cmd->length = blocksize*n;
    memcpy(&cmd->data, d, blocksize*n);
    rbbufdata->Write(id, buf, blocksize*n + 2*8 + 2*4);
}
