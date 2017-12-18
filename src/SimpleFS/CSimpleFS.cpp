#include "CSimpleFS.h"
#include "CDirectory.h"
#include "Logger.h"

#include<cassert>
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
    explicit CFragmentOverlap(int64_t _ofs=0, int64_t _size=0) : ofs(_ofs), size(_size) {}
    int64_t ofs;
    int64_t size;
};

bool FindIntersect(const CFragmentOverlap &a, const CFragmentOverlap &b, CFragmentOverlap &i)
{
    if (a.ofs > b.ofs) i.ofs = a.ofs; else i.ofs = b.ofs;
    if (a.ofs+a.size > b.ofs+b.size) i.size = b.ofs+b.size-i.ofs; else i.size = a.ofs+a.size-i.ofs;
    return i.size > 0;
}

// -------------------------------------------------------------

typedef struct
{
    char magic[8];
    int32_t version;
} SUPER;

SimpleFilesystem::SimpleFilesystem(const std::shared_ptr<CCacheIO> &_bio) : bio(_bio), fragmentlist(_bio)
{
    static_assert(sizeof(DIRENTRY) == 128, "");
    static_assert(CFragmentDesc::SIZEONDISK == 16, "");

    nopendir = 0;
    nopenfiles = 0;
    ncreatedir = 0;
    ncreatefiles = 0;
    nread = 0;
    nwritten = 0;
    nrenamed = 0;
    nremoved = 0;
    ntruncated = 0;

    LOG(LogLevel::INFO) << "container info:";
    LOG(LogLevel::INFO) << "  size: " << int(bio->GetFilesize()/(1024*1024)) << " MB";
    LOG(LogLevel::INFO) << "  blocksize: " << bio->blocksize << " bytes";

    CBLOCKPTR superblock = bio->GetBlock(1);
    auto * super = (SUPER*)superblock->GetBufRead();
    if (strncmp(super->magic, "CoverFS", 7) != 0)
    {
        superblock->ReleaseBuf();
        CreateFS();
        return;
    }
    LOG(LogLevel::INFO) << "filesystem " << super->magic << " V" << (super->version>>16) << "." << (super->version|0xFFFF);
    superblock->ReleaseBuf();

    fragmentlist.Load();
}

SimpleFilesystem::~SimpleFilesystem()
{
    LOG(LogLevel::DEBUG) << "SimpleFilesystem: Destruct";
    LOG(LogLevel::INFO) << "Opened files:        " << nopenfiles;
    LOG(LogLevel::INFO) << "Opened directories:  " << nopendir;
    LOG(LogLevel::INFO) << "Created files:       " << ncreatefiles;
    LOG(LogLevel::INFO) << "Created directories: " << ncreatedir;
    LOG(LogLevel::INFO) << "Read commands:       " << nread;
    LOG(LogLevel::INFO) << "Write commands:      " << nwritten;
    LOG(LogLevel::INFO) << "Renamed nodes:       " << nrenamed;
    LOG(LogLevel::INFO) << "Removed nodes:       " << nremoved;
    LOG(LogLevel::INFO) << "Truncated nodes:     " << ntruncated;


    std::lock_guard<std::mutex> lock(inodescachemtx);
    for (auto &inode : inodes)
    {
        while(inode.second.use_count() > 1)
        {
            LOG(LogLevel::WARN) << "Inode with id=" << inode.second->id << "still in use. Filename='" << inode.second->name << "'";
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
    LOG(LogLevel::INFO) << "==================";
    LOG(LogLevel::INFO) << "Create Filesystem";

    LOG(LogLevel::INFO) << "  Write superblock";

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
    int id = rootdir.MakeDirectory(std::string("root"));

    if (id != CFragmentDesc::ROOTID)
    {
        LOG(LogLevel::ERR) << "Error: Cannot create root directory";
        exit(1);
    }
/*
    int id = fragmentlist.ReserveNewFragment(INODETYPE::file);
    INODEPTR node = OpenNode(id);
    GrowNode(*node, 1);
*/

    CDirectory dir = OpenDir("/");
    dir.MakeDirectory("mydir");

    dir.MakeFile("hello");
    INODEPTR node = OpenNode("hello");
    const char *s = "Hello world\n";
    node->Write((int8_t*)s, 0, strlen(s));

    bio->Sync();

    LOG(LogLevel::INFO) << "Filesystem created";
    LOG(LogLevel::INFO) << "==================";
}

INODEPTR SimpleFilesystem::OpenNode(int id)
{
    std::lock_guard<std::mutex> lock(inodescachemtx);

    auto it = inodes.find(id);
    if (it != inodes.end())
    {
        LOG(LogLevel::DEEP) << "Open File with id=" << id << " size=" << it->second->size << "and ptrcount=" << it->second.use_count();
        assert(id == it->second->id);
        return it->second;
    }

    INODEPTR node(new INODE(*this));
    node->id = id;
    node->size = 0;
    node->fragments.clear();
    node->parentid = CFragmentDesc::INVALIDID;
    fragmentlist.GetFragmentIdxList(id, node->fragments, node->size);

    assert(!node->fragments.empty());
    node->type = fragmentlist.fragments[node->fragments[0]].type;
    inodes[id] = node;
    LOG(LogLevel::DEEP) << "Open File with id=" << id << " size=" << node->size;

    if (node->type == INODETYPE::dir) nopendir++; else nopenfiles++;

    return node;
}

std::vector<std::string> SplitPath(const std::string &path)
{
    std::vector<std::string> d;
    std::string s;

    unsigned int idx = 0;

    while(idx<path.size())
    {
        if ((path[idx] == '/') || (path[idx] == '\\'))
        {
            if (!s.empty())
            {
                d.push_back(s);
                s = "";
            }
            idx++;
            continue;
        }
        s += path[idx];
        idx++;
    }
    if (!s.empty()) d.push_back(s);
    /*
        for(unsigned int i=0; i<d.size(); i++)
                printf("  %i: %s\n", i, d[i].c_str());
    */
    return d;
}

INODEPTR SimpleFilesystem::OpenNode(const std::string &path)
{
    assert(!path.empty());
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
    LOG(LogLevel::DEEP) << "Truncate of id=" << node.id << " from:" << node.size << "to:" << size;
    assert(node.id != CFragmentDesc::INVALIDID);
    if (size == node.size) return;

    if (size > node.size)
    {
        int64_t ofs = node.size;
        GrowNode(node, size);
        if (!dozero) return;

        int64_t fragmentofs = 0x0;
        for (int idx : node.fragments) {
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
    for (int idx : node.fragments) {
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

int SimpleFilesystem::MakeFile(CDirectory &dir, const std::string &name)
{
    int id = CreateNode(dir, name, INODETYPE::file);
    LOG(LogLevel::DEEP) << "Create File '" << name << "' with id=" << id;
    return id;
}

int SimpleFilesystem::MakeDirectory(CDirectory &dir, const std::string &name)
{
    int id = CreateNode(dir, name, INODETYPE::dir);
    LOG(LogLevel::DEEP) << "Create Directory '" << name << "' with id=" << id;

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

void SimpleFilesystem::StatFS(CStatFS *buf)
{
    buf->f_bsize   = bio->blocksize;
    buf->f_frsize  = bio->blocksize;
    buf->f_namemax = 64+31;

    int64_t totalsize = bio->GetFilesize() + (100LL * 0x40000000LL); // add always 100GB
    buf->f_blocks  = totalsize/bio->blocksize;

    std::lock_guard<std::mutex> lock(fragmentlist.fragmentsmtx);

    std::set<int32_t> s;
    int64_t size=0;
    for (auto &fragment : fragmentlist.fragments) {
        int32_t id = fragment.id;
        if (id >= 0)
        {
            size += fragment.size/bio->blocksize+1;
            s.insert(id);
        }
    }
    buf->f_bfree  = totalsize / bio->blocksize - size;
    buf->f_bavail = totalsize / bio->blocksize - size;
    buf->f_files  = s.size();
}
