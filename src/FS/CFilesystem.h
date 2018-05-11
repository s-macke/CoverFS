#ifndef CFILESYSTEM_H
#define CFILESYSTEM_H

#include<memory>
#include<functional>
#include "CPath.h"

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

enum class INODETYPE : int32_t {undefined=0, dir=1, file=2, special=3};

class CInode
{
    public:
    virtual int64_t Read(int8_t *d, int64_t ofs, int64_t size)=0;
    virtual void Write(const int8_t *d, int64_t ofs, int64_t size)=0;
    virtual void Truncate(int64_t size, bool dozero)=0;
    virtual void Remove()=0;
    virtual int64_t GetSize()=0;
    virtual int32_t GetId()=0;
    virtual INODETYPE GetType()=0;
};
using CInodePtr = std::shared_ptr<CInode>;

class CDirectoryEntry
{
    public:
        CDirectoryEntry(const std::string _name, int32_t _id) : name(_name), id(_id) {}
        CDirectoryEntry() {}
        std::string name;
        int32_t id;

};


class CDirectoryIterator
{
    public:
        virtual bool HasNext()=0;
        virtual CDirectoryEntry Next()=0;
    private:
        CDirectoryEntry de;
};
using CDirectoryIteratorPtr = std::unique_ptr<CDirectoryIterator>;

class CDirectory
{
    public:
        virtual CDirectoryIteratorPtr GetIterator()=0;
        virtual int MakeDirectory(const std::string& name)=0;
        virtual int MakeFile(const std::string& name)=0;
        virtual int32_t GetId()=0;
        virtual void Remove()=0;
        virtual bool IsEmpty()=0;
};
using CDirectoryPtr = std::shared_ptr<CDirectory>;

class CFilesystem
{
    public:
        virtual CInodePtr OpenNode(const CPath &path)=0;
        virtual CInodePtr OpenNode(int id)=0;

        virtual CDirectoryPtr OpenDir(const CPath &path)=0;
        virtual CDirectoryPtr OpenDir(int id)=0;

        virtual CInodePtr OpenFile(const CPath &path)=0;
        virtual CInodePtr OpenFile(int id)=0;

        virtual void Rename(CInodePtr node, CDirectoryPtr newdir, const std::string &filename)=0;
        virtual void StatFS(CStatFS *buf)=0;

        virtual void PrintInfo()=0;
        virtual void PrintFragments()=0;
        virtual void Check()=0;
};
using CFilesystemPtr = std::shared_ptr<CFilesystem>;

#endif