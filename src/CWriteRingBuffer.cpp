#include "CWriteRingBuffer.h"

CWriteRingBuffer::CWriteRingBuffer(ssl_socket &s) : socket(s)
{
    buf.assign(1024*1024, 0);
    pushidx = 0;
    popidx = 0;
    size = 0;
    thread = std::thread(&CWriteRingBuffer::Execute, this, this);
}

void CWriteRingBuffer::Execute(CWriteRingBuffer *object)
{
    std::unique_lock<std::mutex> lock(condmtx);
    unsigned int bufsize = object->buf.size();
    while(1)
    {
        cond.wait_for(lock, std::chrono::milliseconds(500));
        unsigned int size = object->size.load();
        //if (size > 0) printf("write size=%i\n", size);
        while(size >= 10)
        {
            int sendsize = std::min(bufsize-object->popidx, size);
            boost::asio::write(object->socket, boost::asio::buffer(&(object->buf[object->popidx]), sendsize));
            object->popidx += sendsize;
            size -= sendsize;
            object->size.fetch_sub(sendsize);
            if (object->popidx >= bufsize) object->popidx -= bufsize;
        }
        pushblockcond.notify_one();
    }
}

void CWriteRingBuffer::Push(int8_t *d, int n)
{
    std::unique_lock<std::mutex> lock(pushblockcondmtx);
    //printf("Push %i bytes\n", n);

    for(int i=0; i<n; i++)
    {
        buf[pushidx] = d[i];
        pushidx++;
        size.fetch_add(1);
        if (pushidx >= buf.size()) pushidx -= buf.size();

        // ringbufffer full. Block all further evaluations
        if (size.load() > buf.size()-2)
        {
            //printf("ringbuffer full: blocking\n");
            cond.notify_one();
            while(size.load() > buf.size()-2)
            {
                pushblockcond.wait_for(lock, std::chrono::milliseconds(500));
            }
        }
    }
    cond.notify_one();
}
