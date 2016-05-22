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
: CAbstractBlockIO(_blocksize), ctx(io_service, ssl::context::sslv23), s(io_service, ctx), cmdid(0)
{
    ctx.set_verify_mode(boost::asio::ssl::context::verify_peer);
    ctx.load_verify_file("ssl/server.crt");

    s.set_verify_mode(boost::asio::ssl::verify_peer);
    s.set_verify_callback(verify_certificate);

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

    boost::asio::connect(s.lowest_layer(), iter, ec);
    if (ec)
    {
        fprintf(stderr, "Error: Cannot connect to server.\n");
        exit(1);
    }
    s.handshake(boost::asio::ssl::stream_base::client);
    rbbuf = new CNetReadWriteBuffer(s);

    iothread = std::thread([&](){
        boost::asio::io_service::work work(io_service);
        io_service.run();
    });

    GetInfo();
}

CNetBlockIO::~CNetBlockIO()
{
    delete rbbuf;
}

int64_t CNetBlockIO::GetFilesize()
{
    int64_t filesize;
    CommandDesc cmd;
    int8_t data[8];
    int32_t id = cmdid.fetch_add(1);
    cmd.cmd = SIZE;
    std::future<void> fut = rbbuf->Read(id, data, 8);
    rbbuf->Write(id, (int8_t*)&cmd, 4);
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
    std::future<void> fut = rbbuf->Read(id, data, 36);
    rbbuf->Write(id, (int8_t*)&cmd, 4);
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
    std::future<void> fut = rbbuf->Read(id, d, blocksize*n);
    rbbuf->Write(id, (int8_t*)&cmd, 2*4+2*8);
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
    rbbuf->Write(id, buf, blocksize*n + 2*8 + 2*4);
}
