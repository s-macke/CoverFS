#include<cstdio>
#include<cstdlib>
#include<unistd.h>
#include<getopt.h>
#include<memory>

#include"Logger.h"

#include"../interface/CFSHandler.h"
#include"../SimpleFS/CDirectory.h"
#include"../SimpleFS/CPrintCheckRepair.h"
#include"CStatusView.h"
#include"ParallelTest.h"

#include"../webapp/webapp.h"
#include<config.h>

// -----------------------------------------------------------------

static CFSHandler handler;
std::unique_ptr<CStatusView> statusview;

// -----------------------------------------------------------------

void PrintUsage(char *argv[])
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
    #ifdef HAVE_POCO
    printf("  --web               Start Webinterface\n");
    #endif
}


// -----------------------------------------------------------------

static void catch_function(int signo)
{
    LOG(INFO) << "Terminate Signal received";
    handler.Unmount().get();
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
    //bool cryptcache = false;
    bool testfs = false;
#ifdef HAVE_POCO
    bool webinterface = false;
#endif

    strncpy(hostname,   "localhost", 255);
    strncpy(port,       "62000",     255);
    strncpy(backend,    "cvfsserver",  255);
    Logger().Set(INFO);

    mountpoint[0] = 0;

    if (argc < 2)
    {
        PrintUsage(argv);
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
            {"debugdeep",  no_argument,       0,  0 },
            {"cryptcache", no_argument,       0,  0 },
            {"test",       no_argument,       0,  0 },
            {"web",        no_argument,       0,  0 },
            {0,            0,                 0,  0 }
        };

    while(true)
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
                    for (char *p=backend ; *p; ++p) *p = static_cast<char>(tolower(*p));
                    break;

                case 8:
                    Logger().Set(DEBUG);
                    break;

                case 9:
                    Logger().Set(DEEP);
                    break;

                case 10:
                    //cryptcache = true;
                    break;

                case 11:
                    testfs = true;
                    break;

                case 12:
                    #ifdef HAVE_POCO
                    webinterface = true;
                    #endif
                    break;

                case 0: // help
                default:
                    PrintUsage(argv);
                    return EXIT_FAILURE;
            }
        break;

        case '?':
        default:
            PrintUsage(argv);
            return EXIT_FAILURE;
        }
    }

    LOG(INFO) << "Start CoverFS";

    #ifdef HAVE_POCO
    if (webinterface)
    {
        return StartWebApp(handler);
        //return EXIT_SUCCESS;
    }
    #endif

    if ((!check) && (!info) && (!showfragments) && (!rootdir) && (!testfs))
    {
        if (optind < argc)
        {
            strncpy(mountpoint, argv[optind], 255);
        } else
        {
            PrintUsage(argv);
            return EXIT_FAILURE;
        }
    }

    bool success = true;
    if (strcmp(backend, "ram") == 0)
    {
        success = handler.ConnectRAM().get();
    } else
    if (strncmp(backend, "file", 255) == 0)
    {
        LOG(ERROR) << "Backend 'file' not supported";
        return EXIT_FAILURE;
    } else
    if (strncmp(backend, "cvfsserver", 255) == 0)
    {
        success = handler.ConnectNET(hostname, port).get();
    } else
    {
        LOG(ERROR) << "Backend '" << backend << "' not supported";
        return EXIT_FAILURE;
    }

    if (!success) return EXIT_FAILURE;

    char *pass = getpass("Password: ");
    bool ret = handler.Decrypt(pass).get();
    memset(pass, 0, strlen(pass));
    if (!ret) return EXIT_FAILURE;


    if (info)
    {
        printf("==============================\n");
        printf("============ INFO ============\n");
        printf("==============================\n");
        CPrintCheckRepair(*handler.fs).PrintInfo();
    }
    if (showfragments)
    {
        printf("==============================\n");
        printf("========= FRAGMENTS ==========\n");
        printf("==============================\n");
        CPrintCheckRepair(*handler.fs).PrintFragments();
    }
    if (check)
    {
        printf("==============================\n");
        printf("=========== CHECK ============\n");
        printf("==============================\n");
        CPrintCheckRepair(*handler.fs).Check();
    }
    if (rootdir)
    {
        CDirectory dir = handler.fs->OpenDir("/");
        dir.List();
    }
    if (testfs)
    {
        printf("==============================\n");
        printf("============ TEST ============\n");
        printf("==============================\n");
        ParallelTest(10, 10, 2000, *handler.fs);
        statusview = std::make_unique<CStatusView>(handler.fs, handler.cbio, handler.bio);
        //std::this_thread::sleep_for(std::chrono::seconds(20));
    }

    if ((info) || (showfragments) || (check) || (rootdir) || (testfs))
    {
        LOG(INFO) << "Stop CoverFS";
        return EXIT_SUCCESS;
    }

    statusview = std::make_unique<CStatusView>(handler.fs, handler.cbio, handler.bio);

    if (signal(SIGINT, catch_function) == SIG_ERR)
    {
        LOG(ERROR) << "An error occurred while setting a signal handler.";
        return EXIT_FAILURE;
    }
    handler.Mount(argc, argv, mountpoint).get();

    LOG(INFO) << "Stop CoverFS";
}
