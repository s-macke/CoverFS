#include "CSimpleFS.h"
#include "CDirectory.h"
#include "Logger.h"

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

SimpleFilesystem::SimpleFilesystem(const std::shared_ptr<CCacheIO> &_bio) : bio(_bio), fragmentlist(_bio)
{
    assert(sizeof(DIRENTRY) == 128);
    assert(CFragmentDesc::SIZEONDISK == 16);

    nopendir = 0;
    nopenfiles = 0;
    ncreatedir = 0;
    ncreatefiles = 0;
    nread = 0;
    nwritten = 0;
    nrenamed = 0;
    nremoved = 0;
    ntruncated = 0;

    LOG(INFO) << "container info:";
    LOG(INFO) << "  size: " << int(bio->GetFilesize()/(1024*1024)) << " MB";
    LOG(INFO) << "  blocksize: " << bio->blocksize << " bytes";

    CBLOCKPTR superblock = bio->GetBlock(1);
    SUPER* super = (SUPER*)superblock->GetBufRead();
    if (strncmp(super->magic, "CoverFS", 7) != 0)
    {
        superblock->ReleaseBuf();
        CreateFS();
        return;
    }
    LOG(INFO) << "filesystem " << super->magic << " V" << (super->version>>16) << "." << (super->version|0xFFFF);
    superblock->ReleaseBuf();

    fragmentlist.Load();
    return;
}

SimpleFilesystem::~SimpleFilesystem()
{
    LOG(DEBUG) << "SimpleFilesystem: Destruct";
    LOG(INFO) << "Opened files:        " << nopenfiles;
    LOG(INFO) << "Opened directories:  " << nopendir;
    LOG(INFO) << "Created files:       " << ncreatefiles;
    LOG(INFO) << "Created directories: " << ncreatedir;
    LOG(INFO) << "Read commands:       " << nread;
    LOG(INFO) << "Write commands:      " << nwritten;
    LOG(INFO) << "Renamed nodes:       " << nrenamed;
    LOG(INFO) << "Removed nodes:       " << nremoved;
    LOG(INFO) << "Truncated nodes:     " << ntruncated;


    std::lock_guard<std::mutex> lock(inodescachemtx);
    for (auto &inode : inodes)
    {
        while(inode.second.use_count() > 1)
        {
            LOG(WARN) << "Inode with id=" << inode.second->id << "still in use. Filename='" << inode.second->name << "'";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

int64_t SimpleFilesystem::GetNInodes()
{
    return inodes.size();
}

void SimpleFilesystem::CreateFS()
{
    LOG(INFO) << "==================";
    LOG(INFO) << "Create Filesystem";

    LOG(INFO) << "  Write superblock";

    CBLOCKPTR superblock = bio->GetBlock(1);
    SUPER* super = (SUPER*)superblock->GetBufReadWrite();
    memset(super, 0, sizeof(SUPER));
    strncpy(super->magic, "CoverFS", 8);
    super->version = (1<<16) | 0;
    superblock->ReleaseBuf();
    bio->Sync();
    fragmentlist.Create();

    // Create root directory

    INODEPTR rootinode(new INODE(*this));
    rootinode->id = CFragmentDesc::INVALIDID;
    rootinode->type = INODETYPE::dir;
    CDirectory rootdir = CDirectory(rootinode, *this);
    int id = rootdir.CreateDirectory(std::string("root"));

    if (id != CFragmentDesc::ROOTID)
    {
        LOG(ERROR) << "Error: Cannot create root directory";
        exit(1);
    }
/*
    int id = fragmentlist.ReserveNewFragment(INODETYPE::file);
    INODEPTR node = OpenNode(id);
    GrowNode(*node, 1);
*/

    CDirectory dir = OpenDir("/");
    dir.CreateDirectory("mydir");

    dir.CreateFile("hello");
    INODEPTR node = OpenNode("hello");
    const char *s = "Hello world\n";
    node->Write((int8_t*)s, 0, strlen(s));

    bio->Sync();

    LOG(INFO) << "Filesystem created";
    LOG(INFO) << "==================";
}

INODEPTR SimpleFilesystem::OpenNode(int id)
{
    std::lock_guard<std::mutex> lock(inodescachemtx);

    auto it = inodes.find(id);
    if (it != inodes.end())
    {
        LOG(DEEP) << "Open File with id=" << id << " size=" << it->second->size << "and ptrcount=" << it->second.use_count();
        assert(id == it->second->id);
        return it->second;
    }

    INODEPTR node(new INODE(*this));
    node->id = id;
    node->size = 0;
    node->fragments.clear();
    node->parentid = CFragmentDesc::INVALIDID;
    fragmentlist.GetFragmentIdxList(id, node->fragments, node->size);

    assert(node->fragments.size() > 0);
    node->type = fragmentlist.fragments[node->fragments[0]].type;
    inodes[id] = node;
    LOG(DEEP) << "Open File with id=" << id << " size=" << node->size;

    if (node->type == INODETYPE::dir) nopendir++; else nopenfiles++;

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
    for(unsigned int i=0; i<splitpath.size(); i++)
    {
        dirid = e.id;
        node = OpenNode(dirid);
        CDirectory(node, *this).Find(splitpath[i], e);
        if (e.id == CFragmentDesc::INVALIDID)
        {
            throw ENOENT;
        }
        if (i<splitpath.size()-1) assert(node->type == INODETYPE::dir);
    }

    node = OpenNode(e.id);
    std::lock_guard<std::mutex> lock(node->GetMutex());
    node->parentid = dirid;
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
        uint64_t nextofs = fragmentlist.fragments[node.fragments.back()].GetNextFreeBlock(bio->blocksize);
        if (fragmentlist.fragments[node.fragments.back()].size == 0) // empty fragment can be overwritten
        {
            storeidx = node.fragments.back();
            fragmentlist.fragments[storeidx] = fd;
            node.size += fd.size;
            fd = CFragmentDesc(INODETYPE::undefined, CFragmentDesc::FREEID, 0, 0);
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
            node.size += fd.size;
            fd = CFragmentDesc(INODETYPE::undefined, CFragmentDesc::FREEID, 0, 0);
        } else
        {
            node.fragments.push_back(storeidx);
            node.size += fd.size;
        }

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
            r = CFragmentDesc(INODETYPE::undefined, CFragmentDesc::FREEID, 0, 0);
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
    ntruncated++;
    LOG(DEEP) << "Truncate of id=" << node.id << " from:" << node.size << "to:" << size;
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
                bio->Zero(
                    fragmentlist.fragments[idx].ofs*bio->blocksize + (intersect.ofs - fragmentofs),
                    intersect.size);
            }
            fragmentofs += fragmentlist.fragments[idx].size;
        }
    } else
    if (size < node.size)
    {
        ShrinkNode(node, size);
    }
    bio->Sync();
}

// -----------

int64_t SimpleFilesystem::Read(INODE &node, int8_t *d, int64_t ofs, int64_t size)
{
    int64_t s = 0;
    nread++;

    if (size == 0) return size;
    //printf("read node.id=%i node.size=%li read_ofs=%li read_size=%li\n", node.id, node.size, ofs, size);

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
            bio->Read(
                fragmentlist.fragments[idx].ofs*bio->blocksize + (intersect.ofs - fragmentofs),
                intersect.size,
                &d[intersect.ofs-ofs]);
            s += intersect.size;
        }
        fragmentofs += fragmentlist.fragments[idx].size;
    }
    //bio->Sync();
    return s;
}

void SimpleFilesystem::Write(INODE &node, const int8_t *d, int64_t ofs, int64_t size)
{
    nwritten++;
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
            bio->Write(
                fragmentlist.fragments[idx].ofs*bio->blocksize + (intersect.ofs - fragmentofs),
                intersect.size,
                &d[intersect.ofs-ofs]);
        }
        fragmentofs += fragmentlist.fragments[idx].size;
    }
    bio->Sync();
}

// -----------

void SimpleFilesystem::Rename(INODEPTR &node, CDirectory &newdir, const std::string &filename)
{
    nrenamed++;
    DIRENTRY e(filename);
    CDirectory olddir = OpenDir(node->parentid);
    olddir.RemoveEntry(node->name, e);
    strncpy(e.name, filename.c_str(), 64+32);
    newdir.AddEntry(e);
}

int SimpleFilesystem::CreateNode(CDirectory &dir, const std::string &name, INODETYPE t)
{
    // Reserve one block. Necessary even for empty files
    if (t == INODETYPE::dir) ncreatedir++; else ncreatefiles++;
    int id = fragmentlist.ReserveNewFragment(t);
    bio->Sync();
    if (dir.dirnode->id == CFragmentDesc::INVALIDID) return id; // this is the root directory and does not have a parent
    dir.AddEntry(DIRENTRY(name, id));
    return id;
}

int SimpleFilesystem::CreateFile(CDirectory &dir, const std::string &name)
{
    int id = CreateNode(dir, name, INODETYPE::file);
    LOG(DEEP) << "Create File '" << name << "' with id=" << id;
    return id;
}

int SimpleFilesystem::CreateDirectory(CDirectory &dir, const std::string &name)
{
    int id = CreateNode(dir, name, INODETYPE::dir);
    LOG(DEEP) << "Create Directory '" << name << "' with id=" << id;

    INODEPTR newdirnode = OpenNode(id);
    newdirnode->parentid = dir.dirnode->id;
    newdirnode->name = name;
    newdirnode->type = INODETYPE::dir;

    CDirectory(newdirnode, *this).Create();
    return id;
}

void SimpleFilesystem::Remove(INODE &node)
{
    nremoved++;
    // maybe, we have to check the shared_ptr here
    DIRENTRY e("");
    CDirectory dir = OpenDir(node.parentid);
    fragmentlist.FreeAllFragments(node.fragments);
    node.fragments.clear();
    dir.RemoveEntry(node.name, e);
    std::lock_guard<std::mutex> lock(inodescachemtx);
    inodes.erase(node.id); // remove from map
}

INODETYPE SimpleFilesystem::GetType(int32_t type)
{
    return fragmentlist.GetType(type);
}

void SimpleFilesystem::StatFS(struct statvfs *buf)
{
    buf->f_bsize   = bio->blocksize;
    buf->f_frsize  = bio->blocksize;
    buf->f_namemax = 64+31;

    int64_t totalsize = bio->GetFilesize() + (100LL * 0x40000000LL); // add always 100GB
    buf->f_blocks  = totalsize/bio->blocksize;

    std::lock_guard<std::mutex> lock(fragmentlist.fragmentsmtx);

    std::set<int32_t> s;
    int64_t size=0;
    for(unsigned int i=0; i<fragmentlist.fragments.size(); i++)
    {
        int32_t id = fragmentlist.fragments[i].id;
        if (id >= 0)
        {
            size += fragmentlist.fragments[i].size/bio->blocksize+1;
            s.insert(id);
        }
    }
    buf->f_bfree  = totalsize / bio->blocksize - size;
    buf->f_bavail = totalsize / bio->blocksize - size;
    buf->f_files  = s.size();
}
