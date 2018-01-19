#ifndef DOKANOPER_H
#define DOKANOPER_H

#include"../FS/CFilesystem.h"

int StartDokan(int argc, char *argv[], const char* mountpoint, CFilesystem &fs);
int StopDokan();

#endif
