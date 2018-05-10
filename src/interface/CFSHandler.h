#ifndef CFSHANDLER_H
#define CFSHANDLER_H

#include <future>
#include"../IO/CBlockIO.h"
#include"../IO/CNetBlockIO.h"
#include"../IO/CEncrypt.h"
#include"../IO/CCacheIO.h"
#include"../FS/CFilesystem.h"

enum HandlerStatus { DISCONNECTED, CONNECTED, UNMOUNTED, MOUNTED };

enum FilesystemType { SIMPLE, CONTAINER };

class CFSHandler
{
    public:

    std::shared_ptr<CAbstractBlockIO> bio;
    std::shared_ptr<CEncrypt> enc;
    std::shared_ptr<CCacheIO> cbio;
    CFilesystemPtr fs;

    std::future<bool> ConnectNET(const std::string hostname, const std::string port);
    std::future<bool> ConnectRAM();
    std::future<bool> Decrypt(char *pass);
    std::future<int> Mount(int argc, char *argv[], const char *mountpoint);
    std::future<int> Unmount();

    void SetFilesystemType(FilesystemType _filesystemType);

    private:
        HandlerStatus status = DISCONNECTED;
        FilesystemType filesystemType = SIMPLE;

};


#endif