#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<getopt.h>
#include<memory>
#include<memory>

#include"debug.h"
#include"CBlockIO.h"
#include"CNetBlockIO.h"

#include"CEncrypt.h"
#include"ParallelTest.h"

#include"CCacheIO.h"
#include"CSimpleFS.h"
#include"CDirectory.h"
#include"CStatusView.h"
#include"CPrintCheckRepair.h"

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
    printf("  --backend [backend] [backend] can be 'ram', 'file', or 'cvfsserer'\n");
    printf("                      default: 'cvfsserver'\n");
    printf("  --host [hostname]   default: 'localhost'\n");
    printf("  --port [port]       default: '62000'\n");
    printf("  --cryptcache        crypt cache in RAM\n");
    printf("  --info              Prints information about filesystem\n");
    printf("  --fragments         Prints information about the fragments\n");
    printf("  --rootdir           Print root directory\n");
    printf("  --check             Check filesystem\n");
    printf("  --test              Tests filesystem and multi-threading\n");
    printf("  --debug             Debug output\n");
}


// -----------------------------------------------------------------

static void catch_function(int signo)
{
    puts("Terminate Signal received");
    exit(EXIT_SUCCESS);
}

// -----------------------------------------------------------------

int main(int argc, char *argv[])
{
    char hostname[256];
    char mountpoint[256];
    char port[256];
    char backend[256];
    bool check = false;
    bool info = false;
    bool showfragments = false;
    bool rootdir = false;
    bool cryptcache = false;
    bool testfs = false;

    strncpy(hostname,   "localhost", 255);
    strncpy(port,       "62000",     255);
    strncpy(backend,    "cvfsserver",  255);
    mountpoint[0] = 0;

    if (argc < 2)
    {
        PrintUsage(argc, argv);
        return 0;
    }

    static struct option long_options[] = {
            {"help",       no_argument,       0,  0 },
            {"port",       required_argument, 0,  0 },
            {"host",       required_argument, 0,  0 },
            {"info",       no_argument,       0,  0 },
            {"fragments",  no_argument,       0,  0 },
            {"rootdir",    no_argument,       0,  0 },
            {"check",      no_argument,       0,  0 },
            {"backend",    required_argument, 0,  0 },
            {"debug",      no_argument,       0,  0 },
            {"cryptcache", no_argument,       0,  0 },
            {"test",       no_argument,       0,  0 },
            {0,            0,                 0,  0 }
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
                    showfragments = true;
                    break;

                case 5:
                    rootdir = true;
                    break;

                case 6:
                    check = true;
                    break;

                case 7:
                    strncpy(backend, optarg, 255);
                    for (char *p=backend ; *p; ++p) *p = tolower(*p);
                    break;

                case 8:
                    Debug().Set(Debug::INFO);
                    break;

                case 9:
                    cryptcache = true;
                    break;

                case 10:
                    testfs = true;
                    break;

                case 0: // help
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

    if ((!check) && (!info) && (!showfragments) && (!rootdir) && (!testfs))
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

    std::shared_ptr<CAbstractBlockIO> bio;
    if (strcmp(backend, "ram") == 0)
    {
        bio.reset(new CRAMBlockIO(4096));
    } else
    if (strncmp(backend, "file", 255) == 0)
    {
        fprintf(stderr, "Error: Backend 'file' not supported\n");
        return EXIT_FAILURE;
    } else
    if (strncmp(backend, "cvfsserver", 255) == 0)
    {
        bio.reset(new CNetBlockIO(4096, hostname, port));
    } else
    {
        fprintf(stderr, "Error: Backend '%s' not found\n", backend);
        return EXIT_FAILURE;
    }

    std::unique_ptr<CStatusView> statusview;
    CEncrypt enc(*bio);
    std::shared_ptr<CCacheIO> cbio(new CCacheIO(bio, enc, cryptcache));
    std::shared_ptr<SimpleFilesystem> fs(new SimpleFilesystem(cbio));

    if (info)
    {
        printf("==============================\n");
        printf("============ INFO ============\n");
        printf("==============================\n");
        CPrintCheckRepair(*fs).PrintInfo();
    }
    if (showfragments)
    {
        printf("==============================\n");
        printf("========= FRAGMENTS ==========\n");
        printf("==============================\n");
        CPrintCheckRepair(*fs).PrintFragments();
    }
    if (check)
    {
        printf("==============================\n");
        printf("=========== CHECK ============\n");
        printf("==============================\n");
        CPrintCheckRepair(*fs).Check();
    }
    if (rootdir)
    {
        CDirectory dir = fs->OpenDir("/");
        dir.List();
    }
    if (testfs)
    {
        printf("==============================\n");
        printf("============ TEST ============\n");
        printf("==============================\n");
        ParallelTest(10, 10, 2000, *fs);
        statusview.reset(new CStatusView(fs, cbio, bio));
        //std::this_thread::sleep_for(std::chrono::seconds(20));
    }

    if ((info) || (showfragments) || (check) || (rootdir) || (testfs))
    {
        return EXIT_SUCCESS;
    }

    statusview.reset(new CStatusView(fs, cbio, bio));

    if (signal(SIGINT, catch_function) == SIG_ERR)
    {
        fputs("An error occurred while setting a signal handler.\n", stderr);
        return EXIT_FAILURE;
    }

#if !defined(_WIN32) && !defined(_WIN64) && !defined(__CYGWIN__)
    return StartFuse(argc, argv, mountpoint, *fs);
#else
    return StartDokan(argc, argv, mountpoint, *fs);
#endif

}
