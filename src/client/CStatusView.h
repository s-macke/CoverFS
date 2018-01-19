#ifndef CSTATUSVIEW_H
#define CSTATUSVIEW_H

#include "../FS/SimpleFS/CSimpleFS.h"
#include "../IO/CCacheIO.h"

#include<memory>
#include<thread>
#include<future>

class CStatusView
{
    public:
        CStatusView(
            std::weak_ptr<CSimpleFilesystem> _fs,
            std::weak_ptr<CCacheIO> _cbio,
            std::weak_ptr<CAbstractBlockIO> _bio
            );
        ~CStatusView();

    private:
        void Work();

        std::thread t;
        std::promise<void> terminate_signal;
        std::future<void> wait_for_terminate;
        std::weak_ptr<CSimpleFilesystem> fs;
        std::weak_ptr<CCacheIO> cbio;
        std::weak_ptr<CAbstractBlockIO> bio;
};


#endif

