#include<vector>

#include "Logger.h"
#include "CSimpleFS.h"
#include "CSimpleFSDirectory.h"

CSimpleFSDirectory::CSimpleFSDirectory(CSimpleFSInodePtr node, CSimpleFilesystem &_fs) : dirnode(node), fs(_fs)
{
    blocksize = fs.bio->blocksize;
    std::lock_guard<std::mutex> lock(dirnode->GetMutex());
    if (node->type != INODETYPE::dir) throw ENOTDIR;
}

CDirectoryIteratorPtr CSimpleFSDirectory::GetIterator()
{
    return std::make_unique<CSimpleFSDirectoryIterator>(GetInternalIterator());
}

CSimpleFSInternalDirectoryIteratorPtr CSimpleFSDirectory::GetInternalIterator() {
    return std::make_unique<CSimpleFSInternalDirectoryIterator>(*this);
}

void CSimpleFSDirectory::CreateEmptyBlock(int8_t* _buf)
{
    int ndirperblock = blocksize/sizeof(CDirectoryEntryOnDisk);
    CDirectoryEntryOnDisk* buf = (CDirectoryEntryOnDisk*)_buf;
    memset(buf, 0, blocksize);
    for (int i = 0; i<ndirperblock; i++)
    {
        buf[i].id = CFragmentDesc::INVALIDID;
    }
}

void CSimpleFSDirectory::Create()
{
    int8_t buf[blocksize];
    CreateEmptyBlock(buf);
    dirnode->Write(buf, 0, blocksize);
}

int CSimpleFSDirectory::MakeDirectory(const std::string& name)
{
    CDirectoryEntryOnDisk e("");
    Find(name, e);
    if (e.id != CFragmentDesc::INVALIDID) throw EEXIST;
    return fs.MakeDirectory(*this, name);
}

int CSimpleFSDirectory::MakeFile(const std::string& name)
{
    CDirectoryEntryOnDisk e("");
    Find(name, e);
    if (e.id != CFragmentDesc::INVALIDID) throw EEXIST;
    return fs.MakeFile(*this, name);
}

void CSimpleFSDirectory::AddEntry(const CDirectoryEntryOnDisk &denew)
{
    LOG(LogLevel::DEEP) << "AddDirEntry '" << denew.name << "' id=" << denew.id;

    std::lock_guard<std::mutex> lock(dirnode->GetMutex());

    CSimpleFSInternalDirectoryIteratorPtr iterator = GetInternalIterator();
    while(iterator->HasNext())
    {
        uint64_t offset = iterator->GetOffset();
        CDirectoryEntryOnDisk de = iterator->Next();
        if (de.id == CFragmentDesc::INVALIDID)
        {
            //memcpy(&de, &denew, sizeof(CDirectoryEntryOnDisk));
            dirnode->WriteInternal((int8_t*)&denew, offset, sizeof(CDirectoryEntryOnDisk));
            return;
        }
    }
    int8_t buf[blocksize];
    CreateEmptyBlock(buf);
    auto *de = (CDirectoryEntryOnDisk*)buf;
    memcpy(de, &denew, sizeof(CDirectoryEntryOnDisk));
    dirnode->WriteInternal(buf, dirnode->size, blocksize);
}

void CSimpleFSDirectory::RemoveEntry(const std::string &name, CDirectoryEntryOnDisk &e)
{
    e.id = CFragmentDesc::INVALIDID;
    LOG(LogLevel::DEEP) << "RemoveDirEntry '" << name << "' in dir '" << dirnode->name << "'";

    std::lock_guard<std::mutex> lock(dirnode->GetMutex());

    CSimpleFSInternalDirectoryIteratorPtr iterator = GetInternalIterator();
    while(iterator->HasNext()) {
        uint64_t offset = iterator->GetOffset();
        CDirectoryEntryOnDisk de = iterator->Next();

        if (de.id == CFragmentDesc::INVALIDID) continue;
        if (strncmp(de.name, name.c_str(), 64+32) == 0)
        {
            memcpy(&e, &de, sizeof(CDirectoryEntryOnDisk));
            de.id = CFragmentDesc::INVALIDID;
            dirnode->WriteInternal((int8_t*)&de, offset, sizeof(CDirectoryEntryOnDisk));
            return;
        }
    };
}

void CSimpleFSDirectory::Find(const std::string &s, CDirectoryEntryOnDisk &e)
{
    std::lock_guard<std::mutex> lock(dirnode->GetMutex());
    e.id = CFragmentDesc::INVALIDID;
    CSimpleFSInternalDirectoryIteratorPtr iterator = GetInternalIterator();
    while(iterator->HasNext())
    {
        CDirectoryEntryOnDisk de = iterator->Next();
        if (de.id != CFragmentDesc::INVALIDID)
        {
            if (strncmp(de.name, s.c_str(), 64+32) == 0)
            {
                memcpy(&e, &de, sizeof(CDirectoryEntryOnDisk));
                return;
            }
        }

    }
}

bool CSimpleFSDirectory::IsEmpty()
{
    CSimpleFSInternalDirectoryIteratorPtr iterator = GetInternalIterator();
    while(iterator->HasNext())
    {
        if (iterator->Next().id != CFragmentDesc::INVALIDID) return false;
    }
    return true;
}

void CSimpleFSDirectory::List()
{
    printf("  Listing of id=%i, name='%s', with size=%lli\n", dirnode->id, dirnode->name.c_str(), (long long int)dirnode->size);
    int n = -1;
    //const char *typestr[] = {"UNK", "DIR", "FILE"};

    CSimpleFSInternalDirectoryIteratorPtr iterator = GetInternalIterator();
    while(iterator->HasNext()) {
        CDirectoryEntryOnDisk de = iterator->Next();
        n++;
        if (de.id == CFragmentDesc::INVALIDID) continue;
        printf("  %3i: %7i '%s'\n", n, de.id, de.name);
    }
}

int32_t CSimpleFSDirectory::GetId()
{
    return dirnode->GetId();
}

void CSimpleFSDirectory::Remove()
{
    dirnode->Remove();
}

// -----------------------------------------------------------------

CSimpleFSDirectoryIterator::CSimpleFSDirectoryIterator(CSimpleFSInternalDirectoryIteratorPtr &&_iterator) : iterator(std::move(_iterator)), lock(iterator->GetDirectory().dirnode->GetMutex())
{};

bool CSimpleFSDirectoryIterator::HasNext()
{
    while(iterator->HasNext())
    {
        CDirectoryEntryOnDisk deondisk = iterator->Next();
        if (deondisk.id == CFragmentDesc::INVALIDID) continue;
        de.name = std::string(deondisk.name);
        de.id = deondisk.id;
        return true;

    }
    return false;
}

CDirectoryEntry CSimpleFSDirectoryIterator::Next()
{
    return de;
}

// -----------------------------------------------------------------

CSimpleFSInternalDirectoryIterator::CSimpleFSInternalDirectoryIterator(CSimpleFSDirectory &_directory) : directory(_directory)
{
    buf.assign(_directory.blocksize, 0);
    ofs = 0;
    nentriesperblock = _directory.blocksize/sizeof(CDirectoryEntryOnDisk);
    GetNextBlock();
};

bool CSimpleFSInternalDirectoryIterator::HasNext()
{
    return (size != 0);
}



CDirectoryEntryOnDisk CSimpleFSInternalDirectoryIterator::Next()
{
    CDirectoryEntryOnDisk de;
    memcpy(&de, &buf[idx*sizeof(CDirectoryEntryOnDisk)], sizeof(CDirectoryEntryOnDisk));
    idx++;
    if (idx >= nentriesperblock) GetNextBlock();
    return de;
}

void CSimpleFSInternalDirectoryIterator::GetNextBlock()
{
    size = directory.dirnode->ReadInternal(&buf[0], ofs, directory.blocksize);
    assert((size==directory.blocksize) || (size == 0));
    ofs += size;
    idx = 0;
}

uint64_t CSimpleFSInternalDirectoryIterator::GetOffset()
{
    return ofs - size + idx * sizeof(CDirectoryEntryOnDisk);
}

CSimpleFSDirectory& CSimpleFSInternalDirectoryIterator::GetDirectory()
{
    return directory;
}