#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>
#include<assert.h>
#include<sys/stat.h>
#include<thread>
#include<mutex>

#define MAXSIZE 0xFFFFF

// ----------------------

typedef struct
{
    int fd;
    char filename[256];
    int size;
    std::mutex mtx;
    char data[MAXSIZE+2];
} FSTRUCT;

FSTRUCT files[10];
unsigned int niter = 2000;
unsigned int nfiles = 10;

// ----------------------

size_t fsize(int fd) 
{
    struct stat st;
    if(fstat(fd, &st) != 0) {
        return 0;
    }
    return st.st_size;   
}

void Execute(int tid)
{
    char *data = new char[MAXSIZE+2];
    ssize_t retsize;

    //printf("thread %i:\n", tid);
    for(unsigned int iter=0; iter<niter; iter++)
    {
        int cmd = rand()%4;
        int id = rand()%10;
        int ofs = 0;
        files[id].mtx.lock();
        if (files[id].size > 0) ofs = rand() % files[id].size;
        int newsize = rand() & MAXSIZE;
        if (ofs+newsize > MAXSIZE) newsize = MAXSIZE - ofs - 1;
        if (newsize < 0) newsize = 0;

        //printf("cmd=%i id=%i\n", cmd, id);
        switch(cmd)
        {
        case 0: // Truncate
            newsize = rand() & MAXSIZE;
            printf("tid %1i %5i: Truncate %i size=%i\n", tid, iter, id, newsize);
            if (newsize > files[id].size)
            if (newsize > files[id].size)
            {
                memset(&(files[id].data[ files[id].size ]), 0, newsize-files[id].size);
            }
            files[id].size = newsize;
            if (ftruncate(files[id].fd, files[id].size) != 0)
            {
                perror("Error during truncate");
                exit(1);
            }
             break;

        case 1: // write
            printf("tid %1i %5i: write %i ofs=%i size=%i\n", tid, iter, id, ofs, newsize);
            for(int i=0; i<newsize; i++)
            {
                files[id].data[ofs+i] = rand();
            }

            retsize = 0;
            do
            {
                ssize_t ret = pwrite(files[id].fd, &(files[id].data[ofs+retsize]), newsize-retsize, ofs);
                if (ret < 0)
                {
                    perror("Error during write");
                    exit(1);
                }
                retsize += ret;
            } while(retsize < newsize);
            assert(retsize == newsize);
            if (ofs+newsize > files[id].size) files[id].size = ofs+newsize;
            break;

        case 2: // read
            printf("tid %1i %5i: read %i ofs=%i size=%i\n", tid, iter, id, ofs, newsize);
            if (ofs+newsize > files[id].size) newsize = files[id].size - ofs - 1;
            if (newsize < 0) newsize = 0;
            retsize = 0;
            do
            {
                ssize_t ret = pread(files[id].fd, &data[retsize], newsize-retsize, ofs);
                if (ret < 0)
                {
                    perror("Error during read");
                    exit(1);
                }
                retsize += ret;
            } while(retsize < newsize);
            assert(retsize == newsize);
            for(int i=0; i<newsize; i++)
            {
                if (data[i] != files[id].data[ofs+i])
                {
                    printf("read data from file %i does not match at ofs=%i read=%i but should be %i\n", id, ofs+i, data[i], files[id].data[ofs+i]);
                    exit(1);
                }
            }
            break;

        case 3: // check filesize
            printf("tid %1i %5i: filesize %i\n", tid, iter, id);
            if (fsize(files[id].fd) != (unsigned int)files[id].size)
            {
                printf("size of file %i does not match %i %li\n", id, files[id].size, fsize(files[id].fd));
                exit(1);
            }
            break;
        }

        files[id].mtx.unlock();
    }
}


int main(int argc, char *argv[])
{
    unsigned int nthreads = 10;
    if (argc != 3)
    {
        printf("Usage: %s nthreads niter\n", argv[0]);
        return 1;
    }
    nthreads = atoi(argv[1]);
    niter = atoi(argv[2]);
    printf("number of threads: %i\n", nthreads);
    printf("number of iterations per thread: %i\n", niter);
    printf("number of files %i\n", nfiles);

    for(unsigned int i=0; i<nfiles; i++)
    {
        sprintf(files[i].filename, "tests%i.dat", i);
        files[i].fd = open(files[i].filename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
        if (files[i].fd == -1)
        {
            perror("Error during open");
            exit(1);
        }
        files[i].size = 0;
        memset(files[i].data, 0, MAXSIZE+1);
    }

    std::thread *t = new std::thread[nthreads];

    for(unsigned int i=0; i<nthreads; i++)
    {
        t[i] = std::thread(Execute, i);
    }

    for(unsigned int i=0; i<nthreads; i++)
    {
        t[i].join();
    }

    for(unsigned int i=0; i<nfiles; i++)
    {
        close(files[i].fd);
    }

    return 0;
}
