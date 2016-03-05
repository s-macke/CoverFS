#include"CNetBlockIO.h"
#include"CWriteRingBuffer.h"

#include<iostream>

using boost::asio::ip::tcp;

enum COMMAND {READ=0, WRITE=1, SIZE=2, INFO=3};

typedef struct
{
    int32_t cmdlen;
    int32_t id;
    int32_t cmd;
    int32_t dummy;
    int64_t offset;
    int64_t length;
    int32_t data;
} COMMANDSTRUCT;


int GetNextPacket(ssl_socket &sock, int8_t *d, int32_t dsize, int cmdid)
{
    static int8_t data[4096*8];
    static uint32_t dataofs = 0;
    static size_t datalength = 0;

    int32_t packetlen = 0;
    int32_t len = -1;

    for (;;)
    {
        if (dataofs == datalength)
        {
            boost::system::error_code error;
            datalength = sock.read_some(boost::asio::buffer(data, 4096*8), error);
            if (error) break;
            dataofs = 0;
        }
        d[packetlen++] = data[dataofs++];
        if (len == packetlen) return len;

        if ((len == -1) && (packetlen >= 8))
        {
            COMMANDSTRUCT *cmd = (COMMANDSTRUCT*)d;
            len = cmd->cmdlen - 8;
            assert(cmd->id == cmdid);
            packetlen = 0;
            assert((len+4) > 0);
            assert((len) <= dsize);
        }
    }
    return 0;
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
    writerb = new CWriteRingBuffer(s);

    iothread = std::thread([&](){
        boost::asio::io_service::work work(io_service);
        io_service.run();
    });

    GetInfo();
}

size_t CNetBlockIO::GetFilesize()
{
    int64_t filesize;
    COMMANDSTRUCT cmd;
    int8_t data[8];
    cmd.cmdlen = 12;
    cmd.id = cmdid.fetch_add(1);
    cmd.cmd = SIZE;
    mtx.lock();
    writerb->Push((int8_t*)&cmd, cmd.cmdlen);
    int len = GetNextPacket(s, data, 12, cmd.id);
    mtx.unlock();
    assert(len == 8);
    memcpy(&filesize, data, sizeof(filesize)); // to prevent the aliasing warning

    return filesize;
}

void CNetBlockIO::GetInfo()
{
    COMMANDSTRUCT cmd;
    int8_t data[36];
    cmd.cmdlen = 12;
    cmd.id = cmdid.fetch_add(1);
    cmd.cmd = INFO;
    mtx.lock();
    writerb->Push((int8_t*)&cmd, cmd.cmdlen);
    int len = GetNextPacket(s, data, 36, cmd.id);
    mtx.unlock();
    assert(len == 36);
    printf("Connected to '%s'\n", data);
}

void CNetBlockIO::Read(const int blockidx, const int n, int8_t *d)
{
    COMMANDSTRUCT cmd;
    cmd.cmdlen = 4*4+2*8;
    cmd.id = cmdid.fetch_add(1);
    cmd.cmd = READ;
    cmd.offset = blockidx*blocksize;
    cmd.length = blocksize*n;
    mtx.lock();
    //printf("read block %i\n", blockidx);
    writerb->Push((int8_t*)&cmd, cmd.cmdlen);
    int len = GetNextPacket(s, d, blocksize*n, cmd.id);
    mtx.unlock();
    assert(len == (int32_t)blocksize*n);
}

void CNetBlockIO::Write(const int blockidx, const int n, int8_t* d)
{
    int8_t buf[blocksize*n + 2*8 + 4*4];
    COMMANDSTRUCT *cmd = (COMMANDSTRUCT*)buf;
    cmd->cmdlen = blocksize*n + 2*8 + 4*4;
    cmd->id = cmdid.fetch_add(1);
    cmd->cmd = WRITE;
    cmd->offset = blockidx*blocksize;
    cmd->length = blocksize*n;
    memcpy(&cmd->data, d, blocksize*n);
    mtx.lock();
    writerb->Push(buf, blocksize*n + 2*8 + 4*4);
    mtx.unlock();
}

