#ifndef PARALLELTEST_H
#define PARALLELTEST_H

#include"../FS/SimpleFS/CSimpleFS.h"

void ParallelTest(unsigned int nfiles, unsigned int nthreads, unsigned int niter, CSimpleFilesystem &_fs);

#endif
