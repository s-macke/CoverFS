#include "CStatusView.h"

#include <thread>
#include <chrono>

static std::thread t;

void ShowStatusThread(SimpleFilesystem &fs, CCacheIO &cbio)
{
    while(1)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        printf("used inodes: %4lli cached blocks: %5lli dirty blocks: %5lli\n",
            (long long int)fs.GetNInodes(),
            (long long int)cbio.GetNCachedBlocks(),
            (long long int)cbio.GetNDirty());
    }
}

void ShowStatus(SimpleFilesystem &fs, CCacheIO &cbio)
{
    t = std::thread(ShowStatusThread, std::ref(fs), std::ref(cbio));
}
