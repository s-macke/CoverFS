#ifndef CSIMPLEFS_H
#define CSIMPLEFS_H

#include <cstring>
#include <string>
#include <map>
#include <memory>
#include <vector>
#include <mutex>
#include <cstdint>

#include"../IO/CCacheIO.h"
#include"INode.h"
#include"CFragment.h"

class CDirectory;
class SimpleFilesystem;
class INODE;
class DIRENTRY;

// ----------------------------------------------------------
class CStatFS
{
public:
    int f_bsize;
    int f_frsize;
    int f_namemax;
    int64_t f_blocks;
    int64_t f_bfree;
    int64_t f_bavail;
    int64_t f_files;
};

// ----------------------------------------------------------

class SimpleFilesystem
{
    friend class CDirectory;
    friend class INODE;
    friend class CPrintCheckRepair;

public:
    explicit SimpleFilesystem(const std::shared_ptr<CCacheIO> &_bio);
    ~SimpleFilesystem();
    INODETYPE GetType(int id);

    INODEPTR OpenNode(int id);
    INODEPTR OpenNode(const std::string &path);
    INODEPTR OpenNode(std::vector<std::string> splitpath);

    CDirectory OpenDir(int id);
    CDirectory OpenDir(const std::string &path);
    CDirectory OpenDir(std::vector<std::string> splitpath);
    INODEPTR OpenFile(int id);
    INODEPTR OpenFile(const std::string &path);
    INODEPTR OpenFile(std::vector<std::string> splitpath);

    void Rename(INODEPTR &node, CDirectory &newdir, const std::string &filename);
    void StatFS(CStatFS *buf);

    void CreateFS();
    void GetRecursiveDirectories(std::map<int32_t, std::string> &direntries, int id, const std::string &path);

    int64_t GetNInodes();

private:

    int MakeDirectory(CDirectory& dir, const std::string& name);
    int MakeFile(CDirectory& dir, const std::string& name);

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

    // Statistics
    std::atomic<int> nopendir;
    std::atomic<int> nopenfiles;
    std::atomic<int> ncreatedir;
    std::atomic<int> ncreatefiles;
    std::atomic<int> nread;
    std::atomic<int> nwritten;
    std::atomic<int> nrenamed;
    std::atomic<int> nremoved;
    std::atomic<int> ntruncated;
};

std::vector<std::string> SplitPath(const std::string &path);

#endif
