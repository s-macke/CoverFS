#ifndef CFRAGMENT_H
#define CFRAGMENT_H

#include"INode.h"

//enum class FRAGMENTTYPE : int32_t {UNKNOWN=0, DIR=1, FILE=2, FREE=-1};

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
};

#endif
