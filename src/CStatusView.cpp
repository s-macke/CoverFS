#include "CStatusView.h"

#include <chrono>
#include <memory>


CStatusView::CStatusView(
    std::weak_ptr<SimpleFilesystem> _fs,
    std::weak_ptr<CCacheIO> _cbio,
    std::weak_ptr<CAbstractBlockIO> _bio
    ) : fs(_fs), cbio(_cbio), bio(_bio)
{
    wait_for_terminate = terminate_signal.get_future();
    t = std::thread(&CStatusView::Work, this);
}

CStatusView::~CStatusView()
{
    terminate_signal.set_value();
    t.join();
}

void CStatusView::Work()
{
    std::string ninodes;
    std::string ncached;
    std::string ndirty;
    std::string nwritecache;

    while(wait_for_terminate.wait_for(std::chrono::seconds(1)) == std::future_status::timeout)
    {
        if (auto fs = this->fs.lock())
        {
            ninodes = std::to_string(fs->GetNInodes());
        } else
        {
            ninodes = "-";
        }
        if (auto bio = this->bio.lock())
        {
            nwritecache = std::to_string(bio->GetWriteCache());
        } else
        {
            nwritecache = "-";
        }
        if (auto cbio = this->cbio.lock())
        {
            ncached = std::to_string(cbio->GetNCachedBlocks());
            ndirty = std::to_string(cbio->GetNDirty());
        } else
        {
            ncached = "-";
            ndirty = "-";
        }
        printf("used inodes: %s cached blocks: %s dirty blocks: %s write cache: %s\n",
            ninodes.c_str(), ncached.c_str(), ndirty.c_str(), nwritecache.c_str());
    }
}
