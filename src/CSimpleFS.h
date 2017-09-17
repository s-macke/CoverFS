#ifndef CSIMPLEFS_H
#define CSIMPLEFS_H

#include <string.h>
#include <string>
#include <map>
#include <memory>
#include <vector>
#include <mutex>
#include <stdint.h>
#include <sys/statvfs.h>

#include"CCacheIO.h"
#include"INode.h"
#include"CFragment.h"

class CDirectory;
class SimpleFilesystem;
class INODE;
class DIRENTRY;

// ----------------------------------------------------------

class SimpleFilesystem
{
    friend class CDirectory;
    friend class INODE;
    friend class CPrintCheckRepair;

public:
    SimpleFilesystem(const std::shared_ptr<CCacheIO> &_bio);
    ~SimpleFilesystem();
    INODEPTR OpenNode(int id);
    INODEPTR OpenNode(const std::string &path);
    INODEPTR OpenNode(const std::vector<std::string> splitpath);

    CDirectory OpenDir(int id);
    CDirectory OpenDir(const std::string &path);
    CDirectory OpenDir(const std::vector<std::string> splitpath);
    INODEPTR OpenFile(int id);
    INODEPTR OpenFile(const std::string &path);
    INODEPTR OpenFile(const std::vector<std::string> splitpath);

    void Rename(INODEPTR &node, CDirectory &newdir, const std::string &filename);
    void StatFS(struct statvfs *buf);

    void CreateFS();
    void GetRecursiveDirectories(std::map<int32_t, std::string> &direntries, int id, const std::string &path);

    int64_t GetNInodes();

private:

    int CreateDirectory(CDirectory &dir, const std::string &name);
    int CreateFile(CDirectory &dir, const std::string &name);

    int64_t Read(INODE &node, int8_t *d, int64_t ofs, int64_t size);
    void Write(INODE &node, const int8_t *d, int64_t ofs, int64_t size);
    void Truncate(INODE &node, int64_t size, bool dozero);
    void Remove(INODE &node);

    void GrowNode(INODE &node, int64_t size);
    void ShrinkNode(INODE &node, int64_t size);

    int CreateNode(CDirectory &dir, const std::string &name, INODETYPE t);

    std::shared_ptr<CCacheIO> bio;

    std::mutex inodescachemtx;

    CFragmentList fragmentlist;

    std::map<int32_t, INODEPTR > inodes;
};

std::vector<std::string> SplitPath(const std::string &path);

#endif
