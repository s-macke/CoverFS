#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<getopt.h>

#include"CBlockIO.h"
#include"CNetBlockIO.h"

#include"CEncrypt.h"

#include"CCacheIO.h"
#include"CSimpleFS.h"
#include"CDirectory.h"

#if !defined(_WIN32) && !defined(_WIN64) && !defined(__CYGWIN__)
#include"fuseoper.h"
#else
#include"dokanoper.h"
#endif

// -----------------------------------------------------------------

void PrintUsage(int argc, char *argv[])
{
    printf("Usage: %s [options] mountpoint\n", argv[0]);
    printf("Options:\n");
    printf("  --help              Print this help message\n");
    printf("  --host [hostname]   default: 'localhost'\n");
    printf("  --port [port]       default: '62000'\n");
    printf("  --info              Print information about filesystem fragments\n");
    printf("  --rootdir           Print root directory\n");
    printf("  --check             Check filesystem\n");
}


// -----------------------------------------------------------------

int main(int argc, char *argv[])
{
    char hostname[256];
    char mountpoint[256];
    char port[256];
    bool check = false;
    bool info = false;
    bool rootdir = false;

    strncpy(hostname,   "localhost", 255);
    strncpy(port,       "62000",     255);
    mountpoint[0] = 0;

    if (argc < 2)
    {
        PrintUsage(argc, argv);
        return 0;
    }

    static struct option long_options[] = {
            {"help",    no_argument,       0,  0 },
            {"port",    required_argument, 0,  0 },
            {"host",    required_argument, 0,  0 },
            {"info",    no_argument,       0,  0 },
            {"rootdir", no_argument,       0,  0 },
            {"check",   no_argument,       0,  0 },
            {0,         0,                 0,  0 }
        };

    while(1)
    {
        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);
        if (c == -1) break;
        switch (c) 
        {
        case 0:
            switch(option_index)
            {
                case 1:
                    strncpy(port, optarg, 255);
                    break;

                case 2:
                    strncpy(hostname, optarg, 255);
                    break;

                case 3:
                    info = true;
                    break;

                case 4:
                    rootdir = true;
                    break;

                case 5:
                    check = true;
                    break;

                case 0: // helÃpp
                default:
                    PrintUsage(argc, argv);
                    return EXIT_FAILURE;
                    break;
            }
        break;

        case '?':
        default:
            PrintUsage(argc, argv);
            return EXIT_FAILURE;
        }
    }

    if ((!check) && (!info) && (!rootdir))
    {
        if (optind < argc)
        {
            strncpy(mountpoint, argv[optind], 255);
        } else
        {
            PrintUsage(argc, argv);
            return EXIT_FAILURE;
        }
    }

    //CBlockIO bio(4096);
    CNetBlockIO bio(4096, hostname, port);
    CEncrypt enc(bio);
    CCacheIO cbio(bio, enc);
    SimpleFilesystem fs(cbio);

    if (info)
    {
        printf("==============================\n");
        printf("============ INFO ============\n");
        printf("==============================\n");
        fs.PrintFS();
    }
    if (rootdir)
    {
        CDirectory dir = fs.OpenDir("/");
        dir.List();
    }
    if (check)
    {
        printf("==============================\n");
        printf("=========== CHECK ============\n");
        printf("==============================\n");
        fs.CheckFS();
    }

if ((info) || (check) || (rootdir))
{
    return EXIT_SUCCESS;
}


#if !defined(_WIN32) && !defined(_WIN64) && !defined(__CYGWIN__)
    return StartFuse(argc, argv, mountpoint, fs);
#else
    return StartDokan(argc, argv, mountpoint, fs);
#endif

}
