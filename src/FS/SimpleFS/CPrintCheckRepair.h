#ifndef CPRINTCHECKREPAIR_H
#define CPRINTCHECKREPAIR_H

#include"CSimpleFS.h"

class CPrintCheckRepair
{
    private:
        CSimpleFilesystem &fs;
        void GetRecursiveDirectories(std::map<int32_t, std::string> &direntries, int id, const std::string &path);

    public:
        explicit CPrintCheckRepair(CSimpleFilesystem &_fs) : fs(_fs) {};

        void PrintFragments();
        void Check();
        void PrintInfo();

};

#endif

