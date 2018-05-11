//
// Created by sebastian on 09.05.18.
//

#include "ContainerFS.h"
#include "../CFilesystem.h"

#define ROOTDIRINODE 1
#define CONTAINERNODE 2

// ----------------------------------------------------

ContainerFS::ContainerFS(const std::shared_ptr<CCacheIO> &_bio) : bio(_bio)
{
}

ContainerFS::~ContainerFS()
= default;


// ----------------------------------------------------


CInodePtr ContainerFS::OpenNode(int id)
{
    if (id == ROOTDIRINODE)
    {
        return std::make_shared<ContainerFSInode>(bio);
    }
    if (id == CONTAINERNODE)
    {
        return std::make_shared<ContainerFSInode>(bio);
    }
    throw ENOENT;
}

CDirectoryPtr ContainerFS::OpenDir(int id)
{
    if (id == ROOTDIRINODE)
    {
        return std::make_shared<ContainerFSDirectory>();
    }
    throw ENOENT;

}

CInodePtr ContainerFS::OpenFile(int id)
{
    if (id == CONTAINERNODE)
    {
        return std::make_shared<ContainerFSInode>(bio);
    }
    throw ENOENT;
}

// ----------------------------------------------------

void ContainerFS::Rename(const CPath &path, CDirectoryPtr newdir, const std::string &filename)
{
    throw EPERM;
}

void ContainerFS::StatFS(CStatFS *buf)
{
    buf->f_bsize   = bio->blocksize;
    buf->f_frsize  = bio->blocksize;
    buf->f_namemax = 64;

    int64_t totalsize = bio->GetFilesize() + (100LL * 0x40000000LL); // add always 100GB
    buf->f_blocks  = totalsize/bio->blocksize;

    buf->f_bfree  = 0;
    buf->f_bavail = 0;
    buf->f_files  = 1;
}

// ----------------------------------------------------

CInodePtr ContainerFS::OpenNode(const CPath &path)
{
    if (path.GetPath().size() == 0)
        return OpenNode(ROOTDIRINODE);
    if ((path.GetPath().size() == 1) && (path.GetPath()[0] == "container"))
        return OpenNode(CONTAINERNODE);

    throw ENOENT;
}

CDirectoryPtr ContainerFS::OpenDir(const CPath &path)
{
    if (path.GetPath().size() == 0)
        return OpenDir(ROOTDIRINODE);
    throw ENOENT;
}

CInodePtr ContainerFS::OpenFile(const CPath &path)
{
    if (path.GetPath().size() == 0)
        return OpenFile(ROOTDIRINODE);
    if ((path.GetPath().size() == 1) && (path.GetPath()[0] == "container"))
        return OpenFile(CONTAINERNODE);
    throw ENOENT;
}

// ----------------------------------------------------

void ContainerFS::PrintInfo()
{
}

void ContainerFS::PrintFragments()
{
}

void ContainerFS::Check()
{
}

// ----------------------------------------------------

CDirectoryIteratorPtr ContainerFSDirectory::GetIterator()
{
    return std::make_unique<ContainerFSDirectoryIterator>();
}

int ContainerFSDirectory::MakeDirectory(const std::string &name)
{
    throw ENOENT;
}

int ContainerFSDirectory::MakeFile(const std::string &name)
{
    throw ENOENT;
}

int32_t ContainerFSDirectory::GetId()
{
    return ROOTDIRINODE;
}

void ContainerFSDirectory::Remove()
{
    throw ENOENT;
}

bool ContainerFSDirectory::IsEmpty()
{
    return false;
}

// ----------------------------------------------------

ContainerFSInode::ContainerFSInode(const std::shared_ptr<CCacheIO> &bio) : bio(bio) {}

int64_t ContainerFSInode::Read(int8_t *d, int64_t ofs, int64_t size)
{
    bio->Read(ofs+bio->blocksize, size, d);
    return size;
}

void ContainerFSInode::Write(const int8_t *d, int64_t ofs, int64_t size)
{
    bio->Write(ofs+bio->blocksize, size, d);
}

void ContainerFSInode::Truncate(int64_t size, bool dozero)
{
    throw EPERM;
}

void ContainerFSInode::Remove()
{
    throw EPERM;
}

int64_t ContainerFSInode::GetSize()
{
    return bio->GetFilesize()-bio->blocksize;
}

int32_t ContainerFSInode::GetId()
{
    return CONTAINERNODE;
}

INODETYPE ContainerFSInode::GetType()
{
    return INODETYPE::file;
}

// ----------------------------------------------------

bool ContainerFSDirectoryIterator::HasNext() {
    return index==0;
}

CDirectoryEntry ContainerFSDirectoryIterator::Next() {
    index++;
    return CDirectoryEntry("container", CONTAINERNODE);
}
