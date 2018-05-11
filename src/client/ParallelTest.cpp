#include<cstdio>
#include<cstdlib>
#include<cstring>

#include<thread>
#include<mutex>

#include"ParallelTest.h"
#include"../FS/CFilesystem.h"


#define MAXSIZE 0xFFFFF

// ----------------------

static CFilesystem *fs;

// ----------------------

typedef struct
{
    CInodePtr node;
    char filename[256]{};
    int64_t size{};
    std::mutex mtx;
    char data[MAXSIZE+2]{};
    unsigned int g_seed{};
} FSTRUCT;

static FSTRUCT *files;

static unsigned int niter = 2000;
static unsigned int nfiles = 10;
static unsigned int nthreads = 10;

static unsigned int g_seed = 0;

inline int fastrand(unsigned int &g_seed)
{
  g_seed = (214013*g_seed+2531011);
  return (g_seed>>16)&0x7FFF;
}

// ----------------------

void Execute(int tid)
{
    char *data = new char[MAXSIZE+2];
    CDirectoryPtr dir = fs->OpenDir(CPath("/"));

    //printf("thread %i:\n", tid);
    for(unsigned int iter=0; iter<niter; iter++)
    {
        int cmd = fastrand(g_seed)%6;
        int id = fastrand(g_seed)%nfiles;
        int ofs = 0;

        files[id].mtx.lock();
        //printf("cmd=%i id=%i\n", cmd, id);
        switch(cmd)
        {

        case 0: // Truncate
        {
            int newsize = rand() & MAXSIZE;
            printf("tid %1i %5i: Truncate %i size=%i\n", tid, iter, id, newsize);
            if (newsize > files[id].size)
            {
                memset(&(files[id].data[ files[id].size ]), 0, newsize - files[id].size);
            }
            files[id].size = newsize;
            files[id].node->Truncate(newsize, true);
        }
        break;


        case 1: // write
        {
            if (files[id].size > 0) ofs = rand() % files[id].size;
            int newsize = rand() & MAXSIZE;
            if (ofs+newsize > MAXSIZE) newsize = MAXSIZE - ofs - 1;
            if (newsize < 0) newsize = 0;

            printf("tid %1i %5i: write %i ofs=%i size=%i\n", tid, iter, id, ofs, newsize);
            for(int i=0; i<newsize; i++)
            {
                files[id].data[ofs+i] = fastrand(files[id].g_seed);
            }

            files[id].node->Write((int8_t*)&(files[id].data[ofs]), ofs, newsize);
            if (ofs+newsize > files[id].size) files[id].size = ofs+newsize;
        }
        break;

        case 2: // read
        {
            if (files[id].size > 0) ofs = rand() % files[id].size;
            int newsize = rand() & MAXSIZE;
            if (ofs+newsize > MAXSIZE) newsize = MAXSIZE - ofs - 1;
            if (newsize < 0) newsize = 0;

            printf("tid %1i %5i: read %i ofs=%i size=%i\n", tid, iter, id, ofs, newsize);
            if (ofs+newsize > files[id].size) newsize = files[id].size - ofs - 1;
            if (newsize < 0) newsize = 0;
            files[id].node->Read((int8_t*)data, ofs, newsize);

            for(int i=0; i<newsize; i++)
            {
                if (data[i] != files[id].data[ofs+i])
                {
                    printf("read data from file %i does not match at ofs=%i read=%i but should be %i\n", id, ofs+i, data[i], files[id].data[ofs+i]);
                    exit(1);
                }
            }
        }
        break;

        case 3: // check filesize
        {
            printf("tid %1i %5i: filesize %i\n", tid, iter, id);
            if (files[id].node->GetSize() != (unsigned int)files[id].size)
            {
                printf("size of file %i does not match %i %lli\n", id, (int)files[id].size, (long long int)files[id].node->GetSize());
                exit(1);
            }
        }
        break;

        case 4: // rename
        {
            printf("tid %1i %5i: rename %i\n", tid, iter, id);
            char newfilename[256];
            sprintf(newfilename, "tests%02i_%03i.dat", id, rand()%999);
            fs->Rename(files[id].node, dir, newfilename);
            strncpy(files[id].filename, newfilename, 256);
        }
        break;


        case 5: // create&remove
        {
            printf("tid %1i %5i: create&remove %i\n", tid, iter, id);
            char newfilename[256];
            sprintf(newfilename, "tests%02i_check.dat", id);
            dir->MakeFile(newfilename);
            CInodePtr node = fs->OpenFile(CPath(newfilename));
            node->Remove();
        }
        break;

        default: break;
        } // switch
        files[id].mtx.unlock();

    }
    delete[] data;
}


void ParallelTest(unsigned int _nfiles, unsigned int _nthreads, unsigned int _niter, CFilesystem &_fs)
{
    printf("Number of files %i\n", nfiles);
    printf("Number of threads: %i\n", nthreads);
    printf("Number of iterations per thread: %i\n", niter);

    srand (time(NULL));
    g_seed = time(NULL);

    nfiles = _nfiles;
    nthreads = _nthreads;
    niter = _niter;
    fs = &_fs;
    CDirectoryPtr dir = fs->OpenDir(CPath("/"));

    files = new FSTRUCT[nfiles];
    for(unsigned int i=0; i<nfiles; i++)
    {
        files[i].g_seed = 0xA0A0A0+i;
        sprintf(files[i].filename, "tests%02i_%03i.dat", i, 0);
        try
        {
            dir->MakeFile(files[i].filename);
        }
        catch(...)
        {
            // file already exists
        }
        files[i].node = fs->OpenFile(CPath(files[i].filename));
        files[i].node->Truncate(0, true);

        files[i].size = 0;
        memset(files[i].data, 0, MAXSIZE+1);
    }

    auto *t = new std::thread[nthreads];

    for(unsigned int i=0; i<nthreads; i++)
    {
        t[i] = std::thread(Execute, i);
    }

    for(unsigned int i=0; i<nthreads; i++)
    {
        t[i].join();
    }
    delete[] t;
    delete[] files;

    printf("Tests done\n");

}
