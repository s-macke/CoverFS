#include"CNetBlockIO.h"

#include<iostream>

using boost::asio::ip::tcp;

enum COMMAND {READ=0, WRITE=1, SIZE=2};

typedef struct
{
    int32_t cmdlen;
    int32_t cmd;
    int64_t offset;
    int64_t length;
    int32_t data;
} COMMANDSTRUCT;

int8_t* GetNextPacket(ssl_socket &sock)
{
    static int8_t packetbuf[4096*2];
    static int32_t packetlen = 0;

    int8_t data[8192];
    for (;;)
    {
        boost::system::error_code error;
        size_t length = sock.read_some(boost::asio::buffer(data, 8192), error);
        if (error) break;
        for(unsigned int i=0; i<length; i++)
        {
            packetbuf[packetlen++] = data[i];
            if (packetlen < 4) continue;
            int32_t len = ((int32_t*)packetbuf)[0];
            assert(len > 0);
            if (len <= packetlen)
            {
                packetlen = 0;
                return packetbuf;
            }
        }
    }
    return NULL;
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


CNetBlockIO::CNetBlockIO(int _blocksize, const std::string &host, const std::string &port) : CAbstractBlockIO(_blocksize), ctx(io_service, ssl::context::sslv23), s(io_service, ctx)
{
    filesize = 0;
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

    COMMANDSTRUCT cmd;
    cmd.cmdlen = 8;
    cmd.cmd = SIZE;
    boost::asio::write(s, boost::asio::buffer(&cmd, cmd.cmdlen));
    int8_t* data = GetNextPacket(s);
    assert(data != NULL);
    
    filesize = ((int64_t*)&data[4])[0];
    //printf("filesize: %li bytes\n", filesize);
}

size_t CNetBlockIO::GetFilesize()
{
    return filesize;
}

void CNetBlockIO::Read(const int blockidx, int8_t *d)
{
    COMMANDSTRUCT cmd;
    cmd.cmdlen = 3*8;
    cmd.cmd = READ;
    cmd.offset = blockidx*blocksize;
    cmd.length = blocksize;
    mtx.lock();
    //printf("read block %i\n", blockidx);
    boost::asio::write(s, boost::asio::buffer(&cmd, cmd.cmdlen));
    int8_t* data = GetNextPacket(s);
    mtx.unlock();
    uint32_t* size = (uint32_t*)data;
    assert(*size == blocksize+4);
    memcpy(d, data+4, blocksize);
}

void CNetBlockIO::Write(const int blockidx, int8_t* d)
{
    int8_t buf[blocksize+3*8];
    COMMANDSTRUCT *cmd = (COMMANDSTRUCT*)buf;
    cmd->cmdlen = blocksize+3*8;
    cmd->cmd = WRITE;
    cmd->offset = blockidx*blocksize;
    cmd->length = blocksize;
    memcpy(&buf[3*8], d, blocksize);
    mtx.lock();
    boost::asio::write(s, boost::asio::buffer(buf, blocksize+3*8));
    mtx.unlock();
}

