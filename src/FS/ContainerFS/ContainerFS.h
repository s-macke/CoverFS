#ifndef CONTAINERFS_H
#define CONTAINERFS_H


#include <vector>
#include "../CFilesystem.h"
#include "../../IO/CCacheIO.h"

class ContainerFS : public CFilesystem
{

public:

    explicit ContainerFS(const std::shared_ptr<CCacheIO> &_bio);
    ~ContainerFS();

    CInodePtr OpenNode(int id) override;
    CInodePtr OpenNode(const CPath &path) override;

    CDirectoryPtr OpenDir(int id) override;
    CDirectoryPtr OpenDir(const CPath &path) override;

    CInodePtr OpenFile(int id) override;
    CInodePtr OpenFile(const CPath &path) override;

    void PrintInfo() override;
    void PrintFragments() override;
    void Check() override;

    void Rename(const CPath &path, CDirectoryPtr newdir, const std::string &filename) override;
    void StatFS(CStatFS *buf) override;

private:
    std::shared_ptr<CCacheIO> bio;
};


class ContainerFSDirectory : public CDirectory
{
public:
    CDirectoryIteratorPtr GetIterator() override;

    int MakeDirectory(const std::string &name) override;
    int MakeFile(const std::string &name) override;
    int32_t GetId() override;
    void Remove() override;
    bool IsEmpty() override;
};

class ContainerFSInode : public CInode
{
public:
    explicit ContainerFSInode(const std::shared_ptr<CCacheIO> &bio);

    int64_t Read(int8_t *d, int64_t ofs, int64_t size) override;
    void Write(const int8_t *d, int64_t ofs, int64_t size) override;
    void Truncate(int64_t size, bool dozero) override;
    void Remove() override;
    int64_t GetSize() override;
    int32_t GetId() override;
    INODETYPE GetType() override;

private:
    std::shared_ptr<CCacheIO> bio;
};


class ContainerFSDirectoryIterator : public CDirectoryIterator
{
public:
    bool HasNext() override;

    CDirectoryEntry Next() override;
private:
    int index=0;

};

#endif //COVERFS_CONTAINERFS_H
