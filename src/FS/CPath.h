#ifndef CPATH_H
#define CPATH_H


#include <string>
#include <vector>
#include <cassert>

class CPath
{
public:
    CPath(std::vector<std::string> &_splitpath);
    CPath(const std::string &path);

    const std::vector<std::string>& GetPath() const;

private:
    std::vector<std::string> splitpath;
};


#endif
