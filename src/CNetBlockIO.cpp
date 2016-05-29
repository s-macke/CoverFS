#include"CNetBlockIO.h"
#include"CNetReadWriteBuffer.h"

#include<iostream>
#include<future>

using boost::asio::ip::tcp;

enum COMMAND {READ=0, WRITE=1, SIZE=2, INFO=3};

typedef struct
{
    int32_t cmd;
    int32_t dummy;
    int64_t offset;
    int64_t length;
    int32_t data;
} CommandDesc;

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
    std::cout << "Verifying certificate: " << subject_name << "\n";

    return preverified;
}


CNetBlockIO::CNetBlockIO(int _blocksize, const std::string &host, const std::string &port)
: CAbstractBlockIO(_blocksize),
  ctx(io_service, ssl::context::sslv23),
  sctrl(io_service, ctx),
  sdata(io_service, ctx),
  cmdid(0)
{
    ctx.set_verify_mode(boost::asio::ssl::context::verify_peer);
    ctx.load_verify_file("ssl/server.crt");

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
        fprintf(stderr, "Error: Cannot resolve host.\n");
        exit(1);
    }
    printf("Connect to %s\n", host.c_str());

    boost::asio::connect(sctrl.lowest_layer(), iter, ec);
    if (ec)
    {
        fprintf(stderr, "Error: Cannot connect to server. (Control Stream)\n");
        exit(1);
    }
    sctrl.handshake(boost::asio::ssl::stream_base::client);

    boost::asio::connect(sdata.lowest_layer(), iter, ec);
    if (ec)
    {
        fprintf(stderr, "Error: Cannot connect to server. (Data Stream))\n");
        exit(1);
    }
    sdata.handshake(boost::asio::ssl::stream_base::client);

    int priority = 6;
    int ret = setsockopt(sctrl.lowest_layer().native(), SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
    if (ret != 0)
    {
        fprintf(stderr, "Warning: Cannot set socket priority\n");
    }

    rbbufctrl = new CNetReadWriteBuffer(sctrl);
    rbbufdata = new CNetReadWriteBuffer(sdata);

    iothread = std::thread([&](){
        boost::asio::io_service::work work(io_service);
        io_service.run();
    });

    GetInfo();
}

CNetBlockIO::~CNetBlockIO()
{
    delete rbbufctrl;
    delete rbbufdata;
}

int64_t CNetBlockIO::GetFilesize()
{
    int64_t filesize;
    CommandDesc cmd;
    int8_t data[8];
    int32_t id = cmdid.fetch_add(1);
    cmd.cmd = SIZE;
    std::future<void> fut = rbbufctrl->Read(id, data, 8);
    rbbufctrl->Write(id, (int8_t*)&cmd, 4);
    fut.get();
    memcpy(&filesize, data, sizeof(filesize)); // to prevent the aliasing warning

    return filesize;
}

void CNetBlockIO::GetInfo()
{
    CommandDesc cmd;
    int8_t data[36];
    int32_t id = cmdid.fetch_add(1);
    cmd.cmd = INFO;
    std::future<void> fut = rbbufctrl->Read(id, data, 36);
    rbbufctrl->Write(id, (int8_t*)&cmd, 4);
    fut.get();
    printf("Connected to '%s'\n", data);
}

void CNetBlockIO::Read(const int blockidx, const int n, int8_t *d)
{
    CommandDesc cmd;
    int32_t id = cmdid.fetch_add(1);
    cmd.cmd = READ;
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
    CommandDesc *cmd = (CommandDesc*)buf;
    int32_t id = cmdid.fetch_add(1);
    cmd->cmd = WRITE;
    cmd->offset = blockidx*blocksize;
    cmd->length = blocksize*n;
    memcpy(&cmd->data, d, blocksize*n);
    rbbufdata->Write(id, buf, blocksize*n + 2*8 + 2*4);
}
