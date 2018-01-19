#ifndef CFILESYSTEM_H
#define CFILESYSTEM_H

#include<memory>

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

enum class FOREACHENTRYRET {OK, QUIT, WRITEANDQUIT};

class CDirectoryEntry
{
    public:
        std::string name;
        int32_t id;
};

class CDirectory
{
    public:
        virtual void ForEachEntry(std::function<void(CDirectoryEntry &de)> f)=0;
        virtual void ForEachEntryNonBlocking(std::function<void(CDirectoryEntry &de)> f)=0;
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
        virtual CInodePtr OpenNode(int id)=0;
        virtual CInodePtr OpenNode(std::vector<std::string> splitpath)=0;
        virtual CInodePtr OpenNode(const std::string &path)=0;

        virtual CDirectoryPtr OpenDir(int id)=0;
        virtual CDirectoryPtr OpenDir(const std::string &path)=0;
        virtual CDirectoryPtr OpenDir(std::vector<std::string> splitpath)=0;

        virtual CInodePtr OpenFile(int id)=0;
        virtual CInodePtr OpenFile(const std::string &path)=0;
        virtual CInodePtr OpenFile(std::vector<std::string> splitpath)=0;

        virtual void Rename(CInodePtr node, CDirectoryPtr newdir, const std::string &filename)=0;
        virtual void StatFS(CStatFS *buf)=0;
};

std::vector<std::string> SplitPath(const std::string &path);

#endif