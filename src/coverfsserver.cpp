#include <stdio.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <thread>
#include <utility>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/ssl.hpp>

using boost::asio::ip::tcp;

//typedef ssl::stream<tcp::socket> ssl_socket;

typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket;

FILE *fp;
size_t filesize;

enum class COMMAND {read, write, size};

typedef struct
{
    int32_t cmdlen;
    int32_t cmd;
    int64_t offset;
    int64_t length;
    int32_t data;
} COMMANDSTRUCT;

void ParseCommand(char *commandbuf, ssl_socket &sock)
{
    //COMMANDSTRUCT *cmd = reinterpret_cast<COMMANDSTRUCT*>(commandbuf);
    COMMANDSTRUCT *cmd = (COMMANDSTRUCT*)commandbuf;
    assert(cmd->cmdlen >= 8);
    switch((COMMAND)cmd->cmd)
    {
    case COMMAND::read:
        {
            printf("READ ofs=%li size=%li (block: %li)\n", cmd->offset, cmd->length, cmd->offset/4096);
            fseek(fp, cmd->offset, SEEK_SET);
            int8_t *data = new int8_t[cmd->length+4];
            fread(&data[4], cmd->length, 1, fp);
            ((int32_t*)data)[0] = cmd->length+4;
            boost::asio::write(sock, boost::asio::buffer(data, cmd->length+4));
            delete[] data;
            break;
        }
    case COMMAND::write:
        printf("WRITE ofs=%li size=%li (block: %li)\n", cmd->offset, cmd->length, cmd->offset/4096);
        fseek(fp, cmd->offset, SEEK_SET);                        
        fwrite(&cmd->data, cmd->length, 1, fp);
        break;

    case COMMAND::size:
        {
            printf("SIZE\n");
            fseek(fp, 0L, SEEK_END);
            filesize = ftell(fp);
            fseek(fp, 0L, SEEK_SET);
            int32_t data[3];
            data[0] = 12;
            memcpy(&data[1], &filesize, 8);
            boost::asio::write(sock, boost::asio::buffer(data, 12));
            break;
        }

    default:
        printf("UNKNOWN COMMANBD\n");
        fprintf(stderr, "ignore received command %i\n", cmd->cmd);
    }

}

static char commandbuf[4096*2];
static int32_t commandbuflen = 0;

void ParseStream(char *data, int length, ssl_socket &sock)
{
    for(int i=0; i<length; i++) 
    {
        commandbuf[commandbuflen++] = data[i];
        if (commandbuflen < 4) continue;
        int32_t len = ((int32_t*)commandbuf)[0];
        if (len <= commandbuflen)
        {
            //printf("received command with len=%i\n", len);
            ParseCommand(commandbuf, sock);
            memset(commandbuf, 0xFF, commandbuflen);
            commandbuflen = 0;
        }
    }
}


void session(ssl_socket &sock)
{
    const int max_length = 4096*10;
    char data[max_length];
    commandbuflen = 0; // hack, find better solution, only one connection allowed at a time

    try
    {

        for (;;)
        {
            boost::system::error_code error;
            size_t length = sock.read_some(boost::asio::buffer(data, max_length), error);
            /*
                if (error == boost::asio::error::eof) break; // Connection closed cleanly by peer.
                else if (error) throw boost::system::system_error(error); // Some other error.
                */
            if (error) break;
            //printf("read block of size %li\n", length);

            ParseStream(data, length, sock);
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception in thread: " << e.what() << "\n";
    }
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
    ctx.use_certificate_chain_file("ssl/server.crt");
    ctx.use_private_key_file("ssl/server.key", boost::asio::ssl::context::pem);
    ctx.use_tmp_dh_file("ssl/dh1024.pem");

    printf("Start listening on port %i\n", port);
    tcp::acceptor a(io_service, tcp::endpoint(tcp::v4(), port));

    for (;;)
    {
        try
        {
            //tcp::ssl::socket sock(io_service, ctx);
            ssl_socket sock(io_service, ctx);
            //session* new_session = new session(io_service_, context_);
            a.accept(sock.lowest_layer());
            printf("Connection from '%s'. Establish SSL connection\n", sock.lowest_layer().remote_endpoint().address().to_string().c_str());
        
            sock.handshake(boost::asio::ssl::stream_base::server);
        
            //std::thread(session, std::move(sock)).detach();
            session(sock);
            printf("Connection closed\n");
        }
        catch (std::exception& e)
        {
            std::cerr << "Exception: " << e.what() << "\n";
        }
        catch (...) // No matter what happens, continue
        {
            fprintf(stderr, "Error: Unknown connection problem. Connection closed\n");
        }

    }
}

void PrintUsage(int argc, char *argv[])
{
	printf("Usage: %s [port]\n", argv[0]);
	printf("The default port is 62000\n");
}

int main(int argc, char *argv[])
{
    boost::asio::io_service io_service;
    int defaultport = 62000;

    if (argc > 2)
    {
        PrintUsage(argc, argv);
        return 0;
    }
    if (argc == 2)
    {
        defaultport = std::atoi(argv[1]);
    }

    const char filename[] = "cfscontainer";
    fp = fopen(filename, "r+b");

    if (fp == NULL)
    {
        printf("create new file '%s'\n", filename);
        fp = fopen(filename, "wb");
        if (fp == NULL)
        {
            fprintf(stderr, "Error: Cannot create file\n");
            return 1;
        }
        ftruncate(fileno(fp), 4096*3);
        fclose(fp);
        fp = fopen(filename, "r+b");
        if (fp == NULL)
        {
            fprintf(stderr, "Error: Cannot open file\n");
            return 1;
        }
    }

    server(io_service, defaultport);
    return 0;
}
