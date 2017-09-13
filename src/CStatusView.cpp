#include "CStatusView.h"

#include <chrono>
#include <memory>


CStatusView::CStatusView(std::weak_ptr<SimpleFilesystem> _fs, std::weak_ptr<CCacheIO> _cbio) : fs(_fs), cbio(_cbio)
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
    long long int ninodes = 0;
    long long int ncached = 0;
    long long int ndirty = 0;

    while(wait_for_terminate.wait_for(std::chrono::seconds(1)) == std::future_status::timeout)
    {
        if (auto fs = this->fs.lock())
        {
            ninodes = fs->GetNInodes();
        } else
        {
            ninodes = 0;
        }
        if (auto cbio = this->cbio.lock())
        {
            ncached = cbio->GetNCachedBlocks();
            ndirty = cbio->GetNDirty();
        } else
        {
            ncached = 0;
            ndirty = 0;
        }
        printf("used inodes: %4lli cached blocks: %5lli dirty blocks: %5lli\n",
            ninodes, ncached, ndirty);
    }
}
