#include <cstdio>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <thread>
#include <cassert>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Logger.h"

using boost::asio::ip::tcp;

typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket;

FILE *fp;
int64_t filesize;

enum class COMMAND {read, write, size, info, close};

typedef struct
{
    int32_t cmdlen;
    int32_t id;
    int32_t cmd;
    int32_t dummy;
    int64_t offset;
    int64_t length;
    int64_t data;
} COMMANDSTRUCT;

typedef struct
{
    int32_t cmdlen;
    int32_t id;
    int8_t data;
} REPLYCOMMANDSTRUCT;


void ParseCommand(char *commandbuf, ssl_socket &sock)
{
    //COMMANDSTRUCT *cmd = reinterpret_cast<COMMANDSTRUCT*>(commandbuf);
    auto *cmd = (COMMANDSTRUCT*)commandbuf;
    LOG(INFO) << "received command " << cmd->cmd << " with len=" << cmd->cmdlen;

    assert(cmd->cmdlen >= 8);
    switch((COMMAND)cmd->cmd)
    {
    case COMMAND::read:
        {
            //printf("READ ofs=%li size=%li (block: %li)\n", cmd->offset, cmd->length, cmd->offset/4096);
            fseek(fp, cmd->offset, SEEK_SET);
            auto *data = new int8_t[cmd->length+8];
            auto *reply = (REPLYCOMMANDSTRUCT*)data;
            reply->cmdlen = cmd->length+8;
            reply->id = cmd->id;
            size_t nread = fread(&reply->data, cmd->length, 1, fp);
            if (nread == 0)
            {
                filesize = ftell(fp);
                if (cmd->offset+cmd->length >= filesize)
                {
                    LOG(WARN) << "Read outside of file boundary";
                } else
                {
                   throw std::runtime_error(std::string("Cannot read ") + std::to_string(cmd->length) + " bytes from container");
                }
            }
            boost::asio::write(sock, boost::asio::buffer(reply, reply->cmdlen));
            delete[] data;
            break;
        }
    case COMMAND::write:
        {
            //printf("WRITE ofs=%li size=%li (block: %li)\n", cmd->offset, cmd->length, cmd->offset/4096);
            fseek(fp, cmd->offset, SEEK_SET);
            size_t nwrite = fwrite(&cmd->data, cmd->length, 1, fp);
            if (nwrite == 0) {
                throw std::runtime_error(
                        std::string("Cannot write ") + std::to_string(cmd->length) + " bytes into container");
            }
            break;
        }
    case COMMAND::size:
        {
            //printf("SIZE\n");
            fseek(fp, 0L, SEEK_END);
            filesize = ftell(fp);
            fseek(fp, 0L, SEEK_SET);
            int32_t data[4];
            auto *reply = (REPLYCOMMANDSTRUCT*)data;
            reply->cmdlen = 16;
            reply->id = cmd->id;
            memcpy(&reply->data, &filesize, 8);
            boost::asio::write(sock, boost::asio::buffer(reply, reply->cmdlen));
            break;
        }

    case COMMAND::info:
        {
            //printf("INFO\n");
            char data[44];
            memset(data, 0, 44);
            auto *reply = (REPLYCOMMANDSTRUCT*)data;
            reply->cmdlen = 44;
            reply->id = cmd->id;
            strncpy((char*)&reply->data, "CoverFS Server V 1.0", 36);
            boost::asio::write(sock, boost::asio::buffer(reply, reply->cmdlen));
            break;
        }

    case COMMAND::close:
        {
            //printf("CLOSE\n");
            REPLYCOMMANDSTRUCT reply{};
            reply.cmdlen = 8;
            reply.id = cmd->id;
            boost::asio::write(sock, boost::asio::buffer(&reply, reply.cmdlen));
            break;
        }

    }

}

void ParseStream(const char *data, int length, ssl_socket &sock, char *commandbuf, int32_t &commandbuflen)
{
    for(int i=0; i<length; i++)
    {
        commandbuf[commandbuflen++] = data[i];
        if (commandbuflen < 4) continue;
        int32_t len=0;
        memcpy(&len, commandbuf, 4); // to prevent the aliasing warning
        if (len <= commandbuflen)
        {
            ParseCommand(commandbuf, sock);
            memset(commandbuf, 0xFF, commandbuflen);
            commandbuflen = 0;
        }
    }
}


void session(ssl_socket *sock)
{
    const int max_length = 4096*10;
    char data[max_length];

    char commandbuf[4096*2];
    int32_t commandbuflen = 0;

    try
    {
        for (;;)
        {
            boost::system::error_code error;
            size_t length = sock->read_some(boost::asio::buffer(data, max_length), error);
            /*
                if (error == boost::asio::error::eof) break; // Connection closed cleanly by peer.
                else if (error) throw boost::system::system_error(error); // Some other error.
            */
            if (error) break;
            ParseStream(data, length, *sock, commandbuf, commandbuflen);
        }
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Exception in thread: " << e.what();
    }
    LOG(INFO) << "Connection closed";
}

std::string get_password(std::size_t max_length, boost::asio::ssl::context::password_purpose purpose)
{
    char *password = getpass("Password for private key: ");
    return std::string(password);
}

void server(boost::asio::io_service& io_service, unsigned short port)
{
    // Create a context that uses the default paths for
    // finding CA certificates.
    boost::asio::ssl::context ctx(boost::asio::ssl::context::sslv23);
    ctx.set_options(
        boost::asio::ssl::context::default_workarounds
        | boost::asio::ssl::context::no_sslv2
        | boost::asio::ssl::context::single_dh_use);
    ctx.set_password_callback(get_password);
    try
    {
        ctx.use_certificate_chain_file("ssl/server.crt");
        ctx.use_private_key_file("ssl/server.key", boost::asio::ssl::context::pem);
        ctx.use_tmp_dh_file("ssl/dh1024.pem");
    } catch(...)
    {
        LOG(ERROR) << "Cannot open ssl related files";
        throw std::exception();
    }

    LOG(INFO) << "Start listening on port " << port;
    tcp::acceptor a(io_service, tcp::endpoint(tcp::v4(), port));

    for (;;)
    {
        try
        {
            auto *sock = new ssl_socket(io_service, ctx);
            a.accept(sock->lowest_layer());
            LOG(INFO)
                << "Connection from '" 
                << sock->lowest_layer().remote_endpoint().address().to_string()
                << "'. Establish SSL connection";

            sock->handshake(boost::asio::ssl::stream_base::server);

            std::thread(session, sock).detach();
            //session(sock);
        }
        catch (std::exception& e)
        {
            LOG(ERROR) << "Exception: " << e.what();
        }
        catch (...) // No matter what happens, continue
        {
            LOG(ERROR) << "Unknown connection problem. Connection closed";
        }

    }
}

void PrintUsage(char *argv[])
{
    printf("Usage: %s [port]\n", argv[0]);
    printf("The default port is 62000\n");
}

int main(int argc, char *argv[])
{
    static_assert(sizeof(COMMANDSTRUCT) == 40, "");
    boost::asio::io_service io_service;
    int defaultport = 62000;

    if (argc > 2)
    {
        PrintUsage(argv);
        return 0;
    }
    if (argc == 2)
    {
        if (strcmp(argv[1], "--help") == 0)
        {
            PrintUsage(argv);
            return 0;
        }
        defaultport = std::atoi(argv[1]);
    }

    const char filename[] = "cfscontainer";
    fp = fopen(filename, "r+b");

    if (fp == nullptr)
    {
        LOG(INFO) << "create new file '" << filename << "'";
        fp = fopen(filename, "wb");
        if (fp == nullptr)
        {
            LOG(ERROR) << "Cannot create file";
            return 1;
        }
        char data[4096*3] = {0};
        fwrite(data, sizeof(data), 1, fp);
        fclose(fp);
        fp = fopen(filename, "r+b");
        if (fp == nullptr)
        {
            LOG(ERROR) << "Cannot open file";
            return 1;
        }
    }

    try
    {
        server(io_service, defaultport);
    }
    catch(std::exception &e)
    {
        LOG(ERROR) << e.what();
    }
    catch(...)
    {
        LOG(ERROR) << "Unknown exception";
    }
    return 0;
}
