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

FSTRUCT *files;
unsigned int niter = 2000;
unsigned int nfiles = 10;
unsigned int nthreads = 10;

unsigned int g_seed = 0x0;

inline int fastrand()
{
  g_seed = (214013*g_seed+2531011);
  return (g_seed>>16)&0x7FFF;
}

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
        int cmd = rand()%7;
        int id = rand()%nfiles;
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
                files[id].data[ofs+i] = fastrand();
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
                printf("size of file %i does not match %i %lli\n", id, files[id].size, (long long int)fsize(files[id].fd));
                exit(1);
            }
            break;

        case 4: // rename
            printf("tid %1i %5i: rename %i\n", tid, iter, id);
            {
                char newfilename[256];
                sprintf(newfilename, "tests%02i_%03i.dat", id, rand()%999);

                if (rename(files[id].filename, newfilename) != 0)
                {
                    if (errno == EEXIST) break;
                    perror("Error during rename");
                    exit(1);
                }
                strncpy(files[id].filename, newfilename, 256);

            }
            break;

        case 5: // close&open
            printf("tid %1i %5i: close&open %i\n", tid, iter, id);
            {
                if (close(files[id].fd) != 0)
                {
                    perror("Error during close");
                    exit(1);
                }
                files[id].fd = open(files[id].filename, O_RDWR, S_IRWXU);
                if (files[id].fd == -1)
                {
                    perror("Error during open");
                    exit(1);
                }
            }
            break;

        case 6: // create&remove
            printf("tid %1i %5i: create&remove %i\n", tid, iter, id);
            {
                char newfilename[256];
                sprintf(newfilename, "tests%02i_check.dat", id);
                int fd = open(newfilename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
                if (fd == -1)
                {
                    perror("Error during open");
                    exit(1);
                }
                if (close(fd) != 0)
                {
                    perror("Error during close");
                    exit(1);
                }
                if (unlink(newfilename) != 0)
                {
                    perror("Error during unlink");
                    exit(1);
                }
            }
            break;

        } // switch

        files[id].mtx.unlock();
    }
}


int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        printf("Usage: %s nfiles nthreads niter\n", argv[0]);
        return 1;
    }
    nfiles = atoi(argv[1]);
    nthreads = atoi(argv[2]);
    niter = atoi(argv[3]);

    printf("number of files %i\n", nfiles);
    printf("number of threads: %i\n", nthreads);
    printf("number of iterations per thread: %i\n", niter);

    srand (time(NULL));
    g_seed = time(NULL);


    files = new FSTRUCT[nfiles];
    for(unsigned int i=0; i<nfiles; i++)
    {
        sprintf(files[i].filename, "tests%02i_%03i.dat", i, 0);
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
