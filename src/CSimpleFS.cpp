#include "CSimpleFS.h"
#include "CDirectory.h"

#include<stdio.h>
#include<assert.h>
#include<set>

/*
TODO:
    - faster sorted offset list
    - readahead
    - remove should check for shared_ptr number of pointers
    - gcrypt mode is not xts?
    - ReleaseBuf on destroy???
    - limit Cache size
*/

// -------------------------------------------------------------


class CFragmentOverlap
{
    public:
    CFragmentOverlap(int64_t _ofs=0, int64_t _size=0) : ofs(_ofs), size(_size) {}
    int64_t ofs;
    int64_t size;
};

bool FindIntersect(const CFragmentOverlap &a, const CFragmentOverlap &b, CFragmentOverlap &i)
{
    if (a.ofs > b.ofs) i.ofs = a.ofs; else i.ofs = b.ofs;
    if (a.ofs+a.size > b.ofs+b.size) i.size = b.ofs+b.size-i.ofs; else i.size = a.ofs+a.size-i.ofs;
    if (i.size <= 0) return false;
    return true;
}

// -------------------------------------------------------------

typedef struct
{
    char magic[8];
    int32_t version;
} SUPER;

SimpleFilesystem::SimpleFilesystem(CCacheIO &_bio) : bio(_bio), fragmentlist(_bio), nodeinvalid(new INODE(*this))
{
    nodeinvalid->id = CFragmentDesc::INVALIDID;

    assert(sizeof(DIRENTRY) == 128);
    assert(CFragmentDesc::SIZEONDISK == 16);

    printf("container info:\n");
    printf("  size: %i MB\n", int(bio.GetFilesize()/(1024*1024)));
    printf("  blocksize: %i\n", bio.blocksize);

    CBLOCKPTR superblock = bio.GetBlock(1);
    SUPER* super = (SUPER*)superblock->GetBufRead();
    if (strncmp(super->magic, "CoverFS", 7) != 0)
    {
        superblock->ReleaseBuf();
        CreateFS();
        return;
    }
    printf("filesystem %s V%i.%i\n", super->magic, super->version>>16, super->version|0xFFFF);
    superblock->ReleaseBuf();

    fragmentlist.Load();
    return;
}

int64_t SimpleFilesystem::GetNInodes()
{
    return inodes.size();
}

void SimpleFilesystem::CreateFS()
{
    printf("==================\n");
    printf("Create Filesystem\n");

    printf("  Write superblock\n");

    CBLOCKPTR superblock = bio.GetBlock(1);
    SUPER* super = (SUPER*)superblock->GetBufReadWrite();
    memset(super, 0, sizeof(SUPER));
    strncpy(super->magic, "CoverFS", 8);
    super->version = (1<<16) | 0;
    superblock->ReleaseBuf();
    bio.Sync();
    fragmentlist.Create();
   

    // Create root directory

    nodeinvalid->id = CFragmentDesc::INVALIDID;
    nodeinvalid->type = INODETYPE::dir;
    CDirectory rootdir = CDirectory(nodeinvalid, *this);
    int id = rootdir.CreateDirectory(std::string("root"));
    nodeinvalid->id = CFragmentDesc::INVALIDID;
    nodeinvalid->type = INODETYPE::unknown;

    if (id != CFragmentDesc::ROOTID)
    {
        fprintf(stderr, "Error: Cannot create root directory\n");
        exit(1);
    }

    CDirectory dir = OpenDir("/");
    dir.CreateDirectory("mydir");

    dir.CreateFile("hello");
    INODEPTR node = OpenNode("hello");
    const char *s = "Hello world\n";
    node->Write((int8_t*)s, 0, strlen(s));

    bio.Sync();

    printf("Filesystem created\n");
    printf("==================\n");
}

INODEPTR SimpleFilesystem::OpenNode(int id)
{
    std::lock_guard<std::mutex> lock(inodescachemtx);

    auto it = inodes.find(id);
    if (it != inodes.end())
    {
        //it->second->Print();
        //printf("Open File with id=%i blocks=%zu and ptrcount=%li\n", id, it->second->blocks.size(), it->second.use_count());
        assert(id == it->second->id);
        return it->second;
    }

    INODEPTR node(new INODE(*this));
    node->id = id;
    node->size = 0;
    node->fragments.clear();
    node->parentid = CFragmentDesc::INVALIDID;
/*
    if (id == CFragmentDesc::ROOTID)
    {
        node->type = INODETYPE::dir;
    }
*/
    fragmentlist.GetFragmentIdxList(id, node->fragments, node->size);

    assert(node->fragments.size() > 0);
    node->type = fragmentlist.fragments[node->fragments[0]].type;
    inodes[id] = node;
    //printf("Open File with id=%i blocks=%zu\n", id, node->blocks.size());
    return node;
}

std::vector<std::string> SplitPath(const std::string &path)
{
    std::vector<std::string> d;
    std::string s = "";

    unsigned int idx = 0;

    while(idx<path.size())
    {
        if ((path[idx] == '/') || (path[idx] == '\\'))
        {
            if (s.size() != 0)
            {
                d.push_back(s);
                s = "";
            }
            idx++;
            continue;
        }
        s = s + path[idx];
        idx++;
    }
    if (s.size() != 0) d.push_back(s);
    /*
        for(unsigned int i=0; i<d.size(); i++)
                printf("  %i: %s\n", i, d[i].c_str());
*/
    return d;
}

INODEPTR SimpleFilesystem::OpenNode(const std::string &path)
{
    assert(path.size() != 0);
    std::vector<std::string> splitpath;
    splitpath = SplitPath(path);
    return OpenNode(splitpath);
}

INODEPTR SimpleFilesystem::OpenNode(const std::vector<std::string> splitpath)
{
    INODEPTR node;
    DIRENTRY e("");

    int dirid = 0;
    e.id = 0;
    e.type = (int32_t)INODETYPE::dir;
    for(unsigned int i=0; i<splitpath.size(); i++)
    {
        dirid = e.id;
        node = OpenNode(dirid);
        CDirectory(node, *this).Find(splitpath[i], e);
        if (e.id == CFragmentDesc::INVALIDID)
        {
            throw ENOENT;
        }
        if (i<splitpath.size()-1) assert((INODETYPE)e.type == INODETYPE::dir);
    }

    node = OpenNode(e.id);
    std::lock_guard<std::mutex> lock(node->GetMutex());
    node->parentid = dirid;
    //node->type = (INODETYPE)e.type; // static cast?
    if (splitpath.empty())
        node->name = "/";
    else
        node->name = splitpath.back();

    return node;
}

CDirectory SimpleFilesystem::OpenDir(int id)
{
    INODEPTR node = OpenNode(id);
    // The check whether this is a directory is done in the constructor
    return CDirectory(node, *this);
}

CDirectory SimpleFilesystem::OpenDir(const std::string &path)
{
    INODEPTR node = OpenNode(path);
    return CDirectory(node, *this);
}

CDirectory SimpleFilesystem::OpenDir(const std::vector<std::string> splitpath)
{
    INODEPTR node = OpenNode(splitpath);
    return CDirectory(node, *this);
}

INODEPTR SimpleFilesystem::OpenFile(int id)
{
    INODEPTR node = OpenNode(id);
    if (node->type != INODETYPE::file) throw ENOENT;
    if (node->id == CFragmentDesc::INVALIDID) throw ENOENT;
    return node;
}

INODEPTR SimpleFilesystem::OpenFile(const std::string &path)
{
    INODEPTR node = OpenNode(path);
    if (node->id == CFragmentDesc::INVALIDID) throw ENOENT;
    if (node->type != INODETYPE::file) throw ENOENT;
    return node;
}

INODEPTR SimpleFilesystem::OpenFile(const std::vector<std::string> splitpath)
{
    INODEPTR node = OpenNode(splitpath);
    if (node->id == CFragmentDesc::INVALIDID) throw ENOENT;
    if (node->type != INODETYPE::file) throw ENOENT;
    return node;
}


void SimpleFilesystem::GrowNode(INODE &node, int64_t size)
{
    std::lock_guard<std::mutex> lock(fragmentlist.fragmentsmtx);
    while(node.size < size)
    {
        int storeidx = fragmentlist.ReserveNextFreeFragment(node.fragments.back(), node.id, node.type, size-node.size);
        assert(node.fragments.back() != storeidx);
        CFragmentDesc &fd = fragmentlist.fragments[storeidx];

        uint64_t nextofs = fragmentlist.fragments[node.fragments.back()].GetNextFreeBlock(bio.blocksize);
        if (fragmentlist.fragments[node.fragments.back()].size == 0) // empty fragment can be overwritten
        {
            storeidx = node.fragments.back();
            fragmentlist.fragments[storeidx] = fd;
            fd.id = CFragmentDesc::FREEID;
        } else
        if (nextofs == fd.ofs) // merge
        {
            storeidx = node.fragments.back();

            if (node.size+(int64_t)fd.size > 0xFFFFFFFFL)
            {
                exit(1);
                // TODO: check for 4GB bouondary
            }
            fragmentlist.fragments[storeidx].size += fd.size;
            fd.id = CFragmentDesc::FREEID;
        } else
        {
            node.fragments.push_back(storeidx);
        }

        node.size += fd.size;
        fragmentlist.StoreFragment(storeidx);
        fragmentlist.SortOffsets();
    }
}


void SimpleFilesystem::ShrinkNode(INODE &node, int64_t size)
{
    std::lock_guard<std::mutex> lock(fragmentlist.fragmentsmtx); // not interfere with sorted offset list
    while(node.size > 0)
    {
        int lastidx = node.fragments.back();
        CFragmentDesc &r = fragmentlist.fragments[lastidx];
        node.size -= r.size;
        r.size = std::max(size - node.size, (int64_t)0);
        node.size += r.size;

        if ((r.size == 0) && (node.size != 0)) // don't remove last element
        {
            r.id = CFragmentDesc::FREEID;
            fragmentlist.StoreFragment(lastidx);
            node.fragments.pop_back();
        } else
        {
            fragmentlist.StoreFragment(lastidx);
            break;
        }
    }
    fragmentlist.SortOffsets();
}

void SimpleFilesystem::Truncate(INODE &node, int64_t size, bool dozero)
{
    //printf("Truncate of id=%i to:%li from:%li\n", node.id, size, node.size);
    assert(node.id != CFragmentDesc::INVALIDID);
    if (size == node.size) return;

    if (size > node.size)
    {
        int64_t ofs = node.size;
        GrowNode(node, size);
        if (!dozero) return;

        int64_t fragmentofs = 0x0;
        for(unsigned int i=0; i<node.fragments.size(); i++)
        {
            int idx = node.fragments[i];
            CFragmentOverlap intersect;
            if (FindIntersect(CFragmentOverlap(fragmentofs, fragmentlist.fragments[idx].size), CFragmentOverlap(ofs, size-ofs), intersect))
            {
                assert(intersect.ofs >= ofs);
                assert(intersect.ofs >= fragmentofs);
                bio.Zero(
                    fragmentlist.fragments[idx].ofs*bio.blocksize + (intersect.ofs - fragmentofs),
                    intersect.size);
            }
            fragmentofs += fragmentlist.fragments[idx].size;
        }

    } else
    if (size < node.size)
    {
        ShrinkNode(node, size);
    }
    bio.Sync();
}

// -----------

int64_t SimpleFilesystem::Read(INODE &node, int8_t *d, int64_t ofs, int64_t size)
{
    int64_t s = 0;
    //printf("read node.id=%i node.size=%li read_ofs=%li read_size=%li\n", node.id, node.size, ofs, size);

    if (size == 0) return size;

    int64_t fragmentofs = 0x0;
    for(unsigned int i=0; i<node.fragments.size(); i++)
    {
        int idx = node.fragments[i];
        assert(fragmentlist.fragments[idx].id == node.id);
        CFragmentOverlap intersect;
        if (FindIntersect(CFragmentOverlap(fragmentofs, fragmentlist.fragments[idx].size), CFragmentOverlap(ofs, size), intersect))
        {
            assert(intersect.ofs >= ofs);
            assert(intersect.ofs >= fragmentofs);
            bio.Read(
                fragmentlist.fragments[idx].ofs*bio.blocksize + (intersect.ofs - fragmentofs),
                intersect.size,
                &d[intersect.ofs-ofs]);
            s += intersect.size;
        }
        fragmentofs += fragmentlist.fragments[idx].size;
    }
    //bio.Sync();
    return s;
}

void SimpleFilesystem::Write(INODE &node, const int8_t *d, int64_t ofs, int64_t size)
{
    if (size == 0) return;

    //printf("write node.id=%i node.size=%li write_ofs=%li write_size=%li\n", node.id, node.size, ofs, size);

    if (node.size < ofs+size) Truncate(node, ofs+size, false);

    int64_t fragmentofs = 0x0;
    for(unsigned int i=0; i<node.fragments.size(); i++)
    {
        int idx = node.fragments[i];
        CFragmentOverlap intersect;
        if (FindIntersect(CFragmentOverlap(fragmentofs, fragmentlist.fragments[idx].size), CFragmentOverlap(ofs, size), intersect))
        {
            assert(intersect.ofs >= ofs);
            assert(intersect.ofs >= fragmentofs);
            bio.Write(
                fragmentlist.fragments[idx].ofs*bio.blocksize + (intersect.ofs - fragmentofs),
                intersect.size,
                &d[intersect.ofs-ofs]);
        }
        fragmentofs += fragmentlist.fragments[idx].size;
    }
    bio.Sync();
}

// -----------

void SimpleFilesystem::Rename(INODEPTR &node, CDirectory &newdir, const std::string &filename)
{
    DIRENTRY e(filename);
    CDirectory olddir = OpenDir(node->parentid);
    olddir.RemoveEntry(node->name, e);
    strncpy(e.name, filename.c_str(), 64+32);
    /*
    // check if file already exist and remove it
    // this is already done in fuserop
    newdir.Find(filename, e);
    DIRENTRY e(filename);
    if (e.id != CFragmentDesc::INVALIDID)
    {
    }
    */
    newdir.AddEntry(e);
}

int SimpleFilesystem::CreateNode(CDirectory &dir, const std::string &name, INODETYPE t)
{
    // Reserve one block. Necessary even for empty files
    int id = fragmentlist.ReserveNewFragment(t);
    bio.Sync();
    if (dir.dirnode->id == CFragmentDesc::INVALIDID) return id; // this is the root directory and does not have a parent
    dir.AddEntry(DIRENTRY(name, id, t));
    return id;
}

int SimpleFilesystem::CreateFile(CDirectory &dir, const std::string &name)
{
    int id = CreateNode(dir, name, INODETYPE::file);
    //printf("Create File '%s' with id=%i\n", name.c_str(), id);
    return id;
}

int SimpleFilesystem::CreateDirectory(CDirectory &dir, const std::string &name)
{
    int id = CreateNode(dir, name, INODETYPE::dir);
    //printf("Create Directory '%s' with id=%i\n", name.c_str(), id);

    INODEPTR newdirnode = OpenNode(id);
    newdirnode->parentid = dir.dirnode->id;
    newdirnode->name = name;
    newdirnode->type = INODETYPE::dir;

    CDirectory(newdirnode, *this).Create();
    return id;
}

void SimpleFilesystem::Remove(INODE &node)
{
    //maybe, we have to check the shared_ptr here
    DIRENTRY e("");
    CDirectory dir = OpenDir(node.parentid);
    fragmentlist.FreeAllFragments(node.fragments);
    node.fragments.clear();
    dir.RemoveEntry(node.name, e);
    std::lock_guard<std::mutex> lock(inodescachemtx);
    inodes.erase(node.id); // remove from map
}

void SimpleFilesystem::StatFS(struct statvfs *buf)
{
    buf->f_bsize   = bio.blocksize;
    buf->f_frsize  = bio.blocksize;
    buf->f_namemax = 64+31;

    int64_t totalsize = bio.GetFilesize() + (100LL * 0x40000000LL); // add always 100GB
    buf->f_blocks  = totalsize/bio.blocksize;

    std::lock_guard<std::mutex> lock(fragmentlist.fragmentsmtx);

    std::set<int32_t> s;
    int64_t size=0;
    for(unsigned int i=0; i<fragmentlist.fragments.size(); i++)
    {
        int32_t id = fragmentlist.fragments[i].id;
        if (id >= 0)
        {
            size += fragmentlist.fragments[i].size/bio.blocksize+1;
            s.insert(id);
        }
    }
    buf->f_bfree  = totalsize / bio.blocksize - size;
    buf->f_bavail = totalsize / bio.blocksize - size;
    buf->f_files  = s.size();
}
