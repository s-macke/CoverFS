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

void CSimpleFSDirectory::ForEachEntry(std::function<void(CDirectoryEntry &de)> f)
{
    std::lock_guard<std::mutex> lock(dirnode->GetMutex());
    ForEachEntryNonBlocking(f);
}

void CSimpleFSDirectory::ForEachEntryNonBlocking(std::function<void(CDirectoryEntry &de)> f)
{
    int8_t buf[blocksize];
    uint64_t ofs = 0;
    int64_t size = 0;
    do
    {
        size = dirnode->ReadInternal(buf, ofs, blocksize);
        assert((size==blocksize) || (size == 0));
        int ndirperblock = size/sizeof(CDirectoryEntryOnDisk);
        auto *de = (CDirectoryEntryOnDisk*)buf;
        for(int j=0; j<ndirperblock; j++)
        {
            if (de->id == CFragmentDesc::INVALIDID)
            {
                de++;
                continue;
            }
            CDirectoryEntry entry;
            entry.name = std::string(de->name);
            entry.id = de->id;
            f(entry);
            de++;
        }
        ofs += size;
    } while(size == blocksize);
}

void CSimpleFSDirectory::ForEachEntryIntern(std::function<FOREACHENTRYRET(CDirectoryEntryOnDisk &de)> f)
{
    std::lock_guard<std::mutex> lock(dirnode->GetMutex());
    ForEachEntryNonBlockingIntern(f);
}

void CSimpleFSDirectory::ForEachEntryNonBlockingIntern(std::function<FOREACHENTRYRET(CDirectoryEntryOnDisk &de)> f)
{
    int8_t buf[blocksize];
    uint64_t ofs = 0;
    int64_t size = 0;
    do
    {
        size = dirnode->ReadInternal(buf, ofs, blocksize);
        assert((size==blocksize) || (size == 0));
        int ndirperblock = size/sizeof(CDirectoryEntryOnDisk);
        auto *de = (CDirectoryEntryOnDisk*)buf;
        for(int j=0; j<ndirperblock; j++)
        {
            FOREACHENTRYRET ret = f(*de);
            if (ret == FOREACHENTRYRET::QUIT) return;
            if (ret == FOREACHENTRYRET::WRITEANDQUIT)
            {
                dirnode->WriteInternal(buf, ofs, blocksize);
                return;
            }
            de++;
        }
        ofs += size;
    } while(size == blocksize);
}

void CSimpleFSDirectory::Create()
{
    int ndirperblock = blocksize/sizeof(CDirectoryEntryOnDisk);
    CDirectoryEntryOnDisk buf[ndirperblock];
    dirnode->Write((int8_t*)buf, 0, blocksize);
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
    bool written = false;
    std::lock_guard<std::mutex> lock(dirnode->GetMutex());
    ForEachEntryNonBlockingIntern([&](CDirectoryEntryOnDisk &de)
    {
        if (de.id == CFragmentDesc::INVALIDID)
        {
            memcpy(&de, &denew, sizeof(CDirectoryEntryOnDisk));
            written = true;
            //printf("Add entry %s\n", de.name);
            return FOREACHENTRYRET::WRITEANDQUIT;
        }
        return FOREACHENTRYRET::OK;
    });
    if (written) return;
    int8_t buf[blocksize];
    memset(buf, 0xFF, blocksize);
    auto *de = (CDirectoryEntryOnDisk*)buf;
    memcpy(de, &denew, sizeof(CDirectoryEntryOnDisk));
    dirnode->WriteInternal(buf, dirnode->size, blocksize);
}

void CSimpleFSDirectory::RemoveEntry(const std::string &name, CDirectoryEntryOnDisk &e)
{
    e.id = CFragmentDesc::INVALIDID;
    LOG(LogLevel::DEEP) << "RemoveDirEntry '" << name << "' in dir '" << dirnode->name << "'";
    ForEachEntryIntern([&](CDirectoryEntryOnDisk &de)
    {
        if (de.id == CFragmentDesc::INVALIDID) return FOREACHENTRYRET::OK;
        if (strncmp(de.name, name.c_str(), 64+32) == 0)
        {
            memcpy(&e, &de, sizeof(CDirectoryEntryOnDisk));
            de.id = CFragmentDesc::INVALIDID;
            return FOREACHENTRYRET::WRITEANDQUIT;
        }
        return FOREACHENTRYRET::OK;
    });
}

void CSimpleFSDirectory::Find(const std::string &s, CDirectoryEntryOnDisk &e)
{
    e.id = CFragmentDesc::INVALIDID;
    ForEachEntryIntern([&](CDirectoryEntryOnDisk &de)
    {
        if (de.id == CFragmentDesc::INVALIDID) return FOREACHENTRYRET::OK;
        if (strncmp(de.name, s.c_str(), 64+32) == 0)
        {
            memcpy(&e, &de, sizeof(CDirectoryEntryOnDisk));
            return FOREACHENTRYRET::QUIT;
        }
        return FOREACHENTRYRET::OK;
    });
}

bool CSimpleFSDirectory::IsEmpty()
{
    bool empty = true;
    ForEachEntryIntern([&](CDirectoryEntryOnDisk &de)
    {
        if (de.id == CFragmentDesc::INVALIDID) return FOREACHENTRYRET::OK;
        empty = false;
        return FOREACHENTRYRET::QUIT;
    });
    return empty;
}

void CSimpleFSDirectory::List()
{
    printf("  Listing of id=%i, name='%s', with size=%lli\n", dirnode->id, dirnode->name.c_str(), (long long int)dirnode->size);
    int n = -1;
    //const char *typestr[] = {"UNK", "DIR", "FILE"};
    ForEachEntryIntern([&](CDirectoryEntryOnDisk &de)
    {
        n++;
        if (de.id == CFragmentDesc::INVALIDID) return FOREACHENTRYRET::OK;
        printf("  %3i: %7i '%s'\n", n, de.id, de.name);
        return FOREACHENTRYRET::OK;
    });
}

int32_t CSimpleFSDirectory::GetId()
{
    return dirnode->GetId();
}

void CSimpleFSDirectory::Remove()
{
    dirnode->Remove();
}
