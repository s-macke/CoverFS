#ifndef FUSE_H
#define FUSE_H

#include"../FS/CFilesystem.h"

int StartFuse(int argc, char *argv[], const char* mountpoint, CFilesystem &fs);
int StopFuse();

#endif
