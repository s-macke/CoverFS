#ifndef CFRAGMENT_H
#define CFRAGMENT_H

#include"INode.h"

#include"CCacheIO.h"


// this is the structure on the hard drive
class CFragmentDesc
{
    public:
    CFragmentDesc(INODETYPE _type, int32_t _id, uint32_t _ofs=0, uint32_t _size=0) : type(_type), id(_id), size(_size), ofs(_ofs){};

    CFragmentDesc(int8_t *ram)
    {
        id   = *(int32_t*)          (ram+0);
        size = *(uint32_t*)         (ram+4);
        ofs  = *((uint64_t*)        (ram+8)) & 0xFFFFFFFFFFFFFF; // 56 bit
        type = (INODETYPE)(*(char*) (ram+15));
    }

    void ToDisk(int8_t *ram)
    {
        *(int32_t*)  (ram+0)  = id;
        *(uint32_t*) (ram+4)  = size;
        *((uint64_t*)(ram+8)) = ofs & 0xFFFFFFFFFFFFFF; // 56 bit
        *(char*)     (ram+15) = (char)type;
    }

    uint64_t GetNextFreeBlock(int blocksize) const { return  ofs + (size-1)/blocksize + 1; };

    INODETYPE type;
    int32_t id;
    uint32_t size; // in bytes
    uint64_t ofs; // in blocks

    static const int SIZEONDISK = 16;

    static const int32_t ROOTID       =  0; // contains the root directory structure
    static const int32_t FREEID       = -1; // this block is not used and can be overwritten
    static const int32_t TABLEID      = -2; // contains the layout tables of the whole filesystem
    static const int32_t SUPERID      = -3; // id of the super block
    static const int32_t INVALIDID    = -4; // defines an invalid id like the parent dir of the root directory
};

class CFragmentList
{
    public:
    CFragmentList(const std::shared_ptr<CCacheIO> &_bio) : bio(_bio) {}

    std::shared_ptr<CCacheIO> bio;

    std::mutex fragmentsmtx;
    std::vector<CFragmentDesc> fragments;
    std::vector<CBLOCKPTR> fragmentblocks;
    std::vector<int> ofssort;

    void Create();
    void Load();
    void StoreFragment(int idx);
    void FreeAllFragments(std::vector<int> &ff);
    int  ReserveNewFragment(INODETYPE type);
    int  ReserveNextFreeFragment(int lastidx, int32_t id, INODETYPE type, int64_t maxsize);
    void GetFragmentIdxList(int32_t id, std::vector<int> &list, int64_t &size);
    INODETYPE GetType(int32_t id);
    void SortOffsets();
};

#endif
