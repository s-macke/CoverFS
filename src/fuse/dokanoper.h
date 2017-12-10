#ifndef DOKANOPER_H
#define DOKANOPER_H

#include"../SimpleFS/CSimpleFS.h"

int StartDokan(int argc, char *argv[], const char* mountpoint, SimpleFilesystem &fs);
int StopDokan();

#endif
