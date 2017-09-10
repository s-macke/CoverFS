#include "CStatusView.h"

#include <thread>
#include <chrono>
#include <memory>

static std::thread t;

void ShowStatusThread(std::weak_ptr<SimpleFilesystem> &pfs, std::weak_ptr<CCacheIO> &pcbio)
{
    long long int ninodes = 0;
    long long int ncached = 0;
    long long int ndirty = 0;

    while(1)
    {
        if (auto fs = pfs.lock())
        {
            ninodes = fs->GetNInodes();
        } else
        {
            ninodes = 0;
        }
        if (auto cbio = pcbio.lock())
        {
            ncached = cbio->GetNCachedBlocks();
            ndirty = cbio->GetNDirty();
        } else
        {
            ncached = 0;
            ndirty = 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        printf("used inodes: %4lli cached blocks: %5lli dirty blocks: %5lli\n",
            ninodes, ncached, ndirty);
    }
}

void ShowStatus(std::weak_ptr<SimpleFilesystem> fs, std::weak_ptr<CCacheIO> cbio)
{
    t = std::thread(ShowStatusThread, std::ref(fs), std::ref(cbio));
}
