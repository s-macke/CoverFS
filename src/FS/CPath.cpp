#include "CPath.h"

CPath::CPath(std::vector<std::string> &_splitpath) : splitpath(_splitpath) {}

CPath::CPath(const std::string &path)
{
    std::vector<std::string> d;
    std::string s;

    assert(!path.empty());

    unsigned int idx = 0;

    while (idx < path.size())
    {
        if ((path[idx] == '/') || (path[idx] == '\\'))
        {
            if (!s.empty())
            {
                d.push_back(s);
                s = "";
            }
            idx++;
            continue;
        }
        s += path[idx];
        idx++;
    }
    if (!s.empty()) d.push_back(s);
/*
    for(unsigned int i=0; i<d.size(); i++)
            printf("  %i: %s\n", i, d[i].c_str());
*/
    splitpath = d;
}

const std::vector<std::string>& CPath::GetPath() const {
    return splitpath;
}
