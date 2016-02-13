#include "CWriteRingBuffer.h"


CWriteRingBuffer::CWriteRingBuffer(ssl_socket &s) : socket(s)
{
    buf.assign(1024*1024, 0);
    pushidx = 0;
    popidx = 0;
    bufsize = 0;
}

void CWriteRingBuffer::AsyncWrite()
{
    wpmutex.lock();
    bool wip = write_in_progress.test_and_set();
    wpmutex.unlock();
    if (wip) return; // return if it already runs

    //if (bufsize.load() > 0) printf("write size=%i\n", bufsize.load());
    int sendsize = std::min((unsigned int)buf.size()-popidx, bufsize.load());
    boost::asio::async_write(
    socket,
    boost::asio::buffer(&(buf[popidx]), sendsize),
    [this](const boost::system::error_code& ec, std::size_t writtenbytes)
    {
        std::unique_lock<std::mutex> lock(condmtx);
        //printf("written bytes %li\n", writtenbytes);
        popidx += writtenbytes;
        bufsize.fetch_sub(writtenbytes);
        if (popidx >= buf.size()) popidx -= buf.size();
        cond.notify_one();
        wpmutex.lock();
        write_in_progress.clear();
        wpmutex.unlock();
        if (bufsize.load() > 0) AsyncWrite();
    });

}

void CWriteRingBuffer::Push(int8_t *d, int n)
{
    std::lock_guard<std::mutex> lock(pushmtx);

    std::unique_lock<std::mutex> lock2(condmtx);
    //printf("Push %i bytes\n", n);

    for(int i=0; i<n; i++)
    {
        // ringbuffer full. Block all further evaluations
        if (bufsize.load() > buf.size()-2)
        {
            printf("ringbuffer full: blocking\n");
            AsyncWrite();
            while(bufsize.load() > buf.size()-2)
            {
                cond.wait_for(lock2, std::chrono::milliseconds(500));
            }
        }

        buf[pushidx] = d[i];
        pushidx++;
        bufsize.fetch_add(1);
        if (pushidx >= buf.size()) pushidx -= buf.size();
    }
    AsyncWrite();
}
