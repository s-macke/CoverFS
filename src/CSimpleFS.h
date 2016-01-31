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

class CDirectory;
class SimpleFilesystem;
class INODE;
class DIRENTRY;

// ----------------------------------------------------------

// this is the structure on the hard drive
class CFragmentDesc
{
    public:
    CFragmentDesc(int32_t _id, uint32_t _ofs=0, uint32_t _size=0) : id(_id), size(_size), ofs(_ofs) {};
    uint64_t GetNextFreeOfs(int blocksize) const { return  ofs + (size-1)/blocksize + 1; };
    int32_t id;
    uint32_t size; // in bytes
    uint64_t ofs; // in blocks
};

// this stores addtionally the index where the entry is stored
class CExtFragmentDesc
{
    public:
    CExtFragmentDesc(int _storeidx, const CFragmentDesc &_be) : storeidx(_storeidx), be(_be) {};
    int storeidx;
    CFragmentDesc be;
};

// ----------------------------------------------------------

class SimpleFilesystem
{
    friend class CDirectory;
    friend class INODE;
public:
    SimpleFilesystem(CCacheIO &_bio);
    INODEPTR OpenNode(const std::string &path);
    INODEPTR OpenNode(const std::vector<std::string> splitpath);

    CDirectory OpenDir(const std::string &path);
    CDirectory OpenDir(const std::vector<std::string> splitpath);
    INODEPTR OpenFile(const std::string &path);
    INODEPTR OpenFile(const std::vector<std::string> splitpath);

    void Rename(INODEPTR &node, CDirectory &newdir, const std::string &filename);
    void StatFS(struct statvfs *buf);

    void CreateFS();
    void CheckFS();
    void PrintFS();

    static const int32_t ROOTID       =  0; // contains the root directory structure
    static const int32_t FREEID       = -1; // this block is not used and can be overwritten
    static const int32_t TABLEID      = -2; // contains the layout tables of the whole filesystem
    static const int32_t SUPERID      = -3; // id of the super block
    static const int32_t INVALIDID    = -4; // defines an invalid id like the parent dir of the root directory

private:
    INODEPTR OpenNode(int id);
    CDirectory OpenDir(int id);

    int CreateDirectory(CDirectory &dir, const std::string &name);
    int CreateFile(CDirectory &dir, const std::string &name);

    int64_t Read(INODE &node, int8_t *d, int64_t ofs, int64_t size);
    void Write(INODE &node, const int8_t *d, int64_t ofs, int64_t size);
    void Truncate(INODE &node, int64_t size, bool dozero);
    void Remove(INODE &node);
    void ReadFragment(int64_t ofs, int8_t *d, int64_t size);
    void WriteFragment(int64_t ofs, const int8_t *d, int64_t size);
    void ZeroFragment(int64_t ofs, const int64_t size);

    void StoreFragment(int idx);

    int ReserveNewFragment();
    CExtFragmentDesc GetNextFreeFragment(INODE &node, int64_t maxsize);

    void SortOffsets();
    void SortIDs();

    void FreeAllFragments(INODE &node);

    int CreateNode(CDirectory &dir, const std::string &name, INODETYPE t);


    CCacheIO &bio;

    std::mutex inodetablemtx;
    std::vector<CBLOCKPTR> fragmentblocks;
    std::vector<CFragmentDesc> fragments;
    std::vector<int> ofssort;
    std::vector<int> idssort;

    std::map<int32_t, INODEPTR > inodes;
    INODEPTR nodeinvalid;
};

std::vector<std::string> SplitPath(const std::string &path);

#endif
