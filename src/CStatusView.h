#ifndef CSTATUSVIEW_H
#define CSTATUSVIEW_H

#include "CSimpleFS.h"
#include "CCacheIO.h"

#include<memory>
#include<thread>
#include<future>

class CStatusView
{
    public:
        CStatusView(std::weak_ptr<SimpleFilesystem> _fs, std::weak_ptr<CCacheIO> _cbio);
        ~CStatusView();

    private:
        void Work();

        std::thread t;
        std::promise<void> terminate_signal;
        std::future<void> wait_for_terminate;
        std::weak_ptr<SimpleFilesystem> fs;
        std::weak_ptr<CCacheIO> cbio;
};


#endif

