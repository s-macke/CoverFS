#include"CFSHandler.h"

#if !defined(_WIN32) && !defined(_WIN64) && !defined(__CYGWIN__)
#include"fuseoper.h"
#else
#include"dokanoper.h"
#endif

std::future<int> CFSHandler::Unmount()
{
    std::future<int> result( std::async([this]{
        try
        {
        #if !defined(_WIN32) && !defined(_WIN64) && !defined(__CYGWIN__)
            return StopFuse();
        #else
            return StopDokan();
        #endif
        } catch(...)
        {
            return EXIT_FAILURE;
        }
    }));
    return result;
}

std::future<int> CFSHandler::Mount(int argc, char *argv[], const char *mountpoint)
{
    std::future<int> result(std::async([this, argc, argv, mountpoint]{
        try
        {
        #if !defined(_WIN32) && !defined(_WIN64) && !defined(__CYGWIN__)
            status = MOUNTED;
            int ret = StartFuse(argc, argv, mountpoint, *fs);
            status = UNMOUNTED;
            return ret;
        #else
            status = MOUNTED;
            int ret = StartDokan(argc, argv, mountpoint, *fs);
            status = UNMOUNTED;
            return ret;
        #endif
        } catch(...)
        {
            return 1;
        }
        
    }));
    return result;
}

std::future<bool> CFSHandler::ConnectNET(const std::string hostname, const std::string port)
{
    std::future<bool> result( std::async([this, hostname, port]{
        try
        {
            bio.reset(new CNetBlockIO(4096, hostname, port));
            status = CONNECTED;
            return true;
        } catch(...)
        {
            return false;
        }
    }));
    return result;
}

std::future<bool> CFSHandler::ConnectRAM()
{
    std::future<bool> result( std::async([this]{
        try
        {
            bio.reset(new CRAMBlockIO(4096));
            status = CONNECTED;
            return true;
        } catch(...)
        {
            return false;
        }
    }));
    return result;
}

std::future<bool> CFSHandler::Decrypt(char *pass)
{
    std::future<bool> result( std::async([this, pass]{
        try
        {
            enc.reset(new CEncrypt(*bio, pass));
            cbio.reset(new CCacheIO(bio, *enc, false));
            fs.reset(new SimpleFilesystem(cbio));
            status = UNMOUNTED;
            return true;
        } catch(...)
        {
            return false;
        }
    }));
    return result;
}
