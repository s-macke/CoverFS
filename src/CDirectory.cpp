#include "CDirectory.h"


// CDirectory allows also invalid nodes, but they will fail if the methods are called

CDirectory::CDirectory(INODEPTR node, SimpleFilesystem &_fs) : dirnode(node), fs(_fs) 
{
    blocksize = fs.bio.blocksize;
    //if (node->id == fs.INVALIDID) throw ENOENT;
    if (node->type != INODETYPE::dir) throw ENOTDIR;
    //dirnode->size = dirnode->blocks.size()*fs.bio.blocksize; // Hack
}


void CDirectory::ForEachEntry(std::function<FOREACHENTRYRET(DIRENTRY &de)> f)
{
    int8_t buf[blocksize];
    std::lock_guard<std::recursive_mutex> lock(dirnode->GetMutex()); // that should be enough

    for(unsigned int i=0; i<dirnode->size/blocksize; i++)
    {
        int64_t size = dirnode->Read(buf, i*blocksize, blocksize);
        assert(size == blocksize);
        int ndirperblock = blocksize/sizeof(DIRENTRY);
        DIRENTRY *de = (DIRENTRY*)buf;
        for(int j=0; j<ndirperblock; j++)
        {
            FOREACHENTRYRET ret = f(*de);
            if (ret == FOREACHENTRYRET::QUIT) return;
            if (ret == FOREACHENTRYRET::WRITEANDQUIT)
            {
                dirnode->Write(buf, i*blocksize, blocksize);
                return;
            }
            de++;
        }
    }
}

void CDirectory::Create()
{
    int8_t buf[blocksize];
    memset(buf, 0xFF, blocksize);
    //dirnode->size = 0; // Some kind of tiny hack to update the file size in the parent dir, but Ok here
    dirnode->Write(buf, 0, blocksize);
}

int CDirectory::CreateDirectory(const std::string &name)
{
    DIRENTRY e("");
    Find(name, e);
    if (e.id != fs.INVALIDID) throw EEXIST;    
    return fs.CreateDirectory(*this, name);
}

int CDirectory::CreateFile(const std::string &name)
{
    DIRENTRY e("");
    Find(name, e);
    if (e.id != fs.INVALIDID) throw EEXIST;    
    return fs.CreateFile(*this, name);
}

void CDirectory::AddEntry(const DIRENTRY &denew)
{
    //printf("AddDirEntry %s size=%li type=%i\n", denew.name, denew.size, denew.type);
    bool written = false;
    std::lock_guard<std::recursive_mutex> lock(dirnode->GetMutex());
    ForEachEntry([&](DIRENTRY &de)
    {
        if ((INODETYPE)de.type == INODETYPE::free)
        {
            memcpy(&de, &denew, sizeof(DIRENTRY));
            written = true;
            //printf("Add entry %s\n", de.name);
            return FOREACHENTRYRET::WRITEANDQUIT;
        }
        return FOREACHENTRYRET::OK;
    });
    if (written) return;
    int8_t buf[blocksize];
    memset(buf, 0xFF, blocksize);
    DIRENTRY *de = (DIRENTRY*)buf;
    memcpy(de, &denew, sizeof(DIRENTRY));
    dirnode->Write(buf, dirnode->size, blocksize);
}

void CDirectory::RemoveEntry(const std::string &name)
{
    //printf("RemoveDirEntry '%s' in dir '%s' and dirid %i\n", name.c_str(), dirnode->name.c_str(), dirnode->id);
    ForEachEntry([&](DIRENTRY &de)
    {
        if ((INODETYPE)de.type == INODETYPE::free) return FOREACHENTRYRET::OK;
        if (strncmp(de.name, name.c_str(), 64+32) == 0)
        {
            de.type = (int32_t)INODETYPE::free;
            return FOREACHENTRYRET::WRITEANDQUIT;
        }
        return FOREACHENTRYRET::OK;
    });
}

void CDirectory::Find(const std::string &s, DIRENTRY &e)
{
    e.id = fs.INVALIDID;
    ForEachEntry([&](DIRENTRY &de)
    {
        if ((INODETYPE)de.type == INODETYPE::free) return FOREACHENTRYRET::OK;
        if (strncmp(de.name, s.c_str(), 64+32) == 0)
        {
            memcpy(&e, &de, sizeof(DIRENTRY));
            return FOREACHENTRYRET::QUIT;
        }
        return FOREACHENTRYRET::OK;
    });
}

bool CDirectory::IsEmpty()
{
    bool empty = true;
    ForEachEntry([&](DIRENTRY &de)
    {
        if ((INODETYPE)de.type == INODETYPE::free) return FOREACHENTRYRET::OK;
        empty = false;
        return FOREACHENTRYRET::QUIT;
    });
    return empty;
}

void CDirectory::List()
{
    printf("  Listing of id=%i, name='%s', with size=%li\n", dirnode->id, dirnode->name.c_str(), dirnode->size);
    int n = -1;
    const char *typestr[] = {"UNK", "DIR", "FILE"};
    ForEachEntry([&](DIRENTRY &de)
    {
        n++;
        if ((INODETYPE)de.type == INODETYPE::free) return FOREACHENTRYRET::OK;
        printf("  %3i: %4s %4i %s\n", n, typestr[de.type], de.id, de.name);
        //printf("  %3i: %s\n", n, de.name);
        return FOREACHENTRYRET::OK;
    });
}
