
#include<cassert>
#include<set>

#include "CSimpleFSInode.h"
#include "CSimpleFSDirectory.h"
#include "CPrintCheckRepair.h"
#include "CSimpleFS.h"
#include "Logger.h"

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

CSimpleFilesystem::CSimpleFilesystem(const std::shared_ptr<CCacheIO> &_bio) : bio(_bio), fragmentlist(_bio)
{
    static_assert(sizeof(CDirectoryEntryOnDisk) == 128, "");
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
    SUPER * super = (SUPER*)superblock->GetBufRead();
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

CSimpleFilesystem::~CSimpleFilesystem()
{
    LOG(LogLevel::DEBUG) << "CSimpleFilesystem: Destruct";
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

int64_t CSimpleFilesystem::GetNInodes()
{
    return inodes.size();
}

void CSimpleFilesystem::CreateFS()
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

    CSimpleFSInodePtr rootinode(new CSimpleFSInode(*this));
    rootinode->id = CFragmentDesc::INVALIDID;
    rootinode->type = INODETYPE::dir;
    CSimpleFSDirectory rootdir = CSimpleFSDirectory(rootinode, *this);
    int id = rootdir.MakeDirectory(std::string("root"));

    if (id != CFragmentDesc::ROOTID)
    {
        LOG(LogLevel::ERR) << "Error: Cannot create root directory";
        exit(1);
    }
/*
    int id = fragmentlist.ReserveNewFragment(INODETYPE::file);
    CSimpleFSInodePtr node = OpenNodeInternal(id);
    GrowNode(*node, 1);
*/
    CDirectoryPtr dir = OpenDir("/");
    dir->MakeDirectory("mydir");

    dir->MakeFile("hello");
    CSimpleFSInodePtr node = OpenNodeInternal("hello");
    const char *s = "Hello world\n";
    node->Write((int8_t*)s, 0, strlen(s));

    bio->Sync();

    LOG(LogLevel::INFO) << "Filesystem created";
    LOG(LogLevel::INFO) << "==================";
}

CSimpleFSInodePtr CSimpleFilesystem::OpenNodeInternal(int id)
{
    std::lock_guard<std::mutex> lock(inodescachemtx);

    auto it = inodes.find(id);
    if (it != inodes.end())
    {
        LOG(LogLevel::DEEP) << "Open File with id=" << id << " size=" << it->second->size << " and ptrcount=" << it->second.use_count();
        assert(id == it->second->id);
        return it->second;
    }

    CSimpleFSInodePtr node(new CSimpleFSInode(*this));
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

CSimpleFSInodePtr CSimpleFilesystem::OpenNodeInternal(const std::string &path)
{
    assert(!path.empty());
    std::vector<std::string> splitpath;
    splitpath = SplitPath(path);
    return OpenNodeInternal(splitpath);
}

CSimpleFSInodePtr CSimpleFilesystem::OpenNodeInternal(const std::vector<std::string> splitpath)
{
    CSimpleFSInodePtr node;
    CDirectoryEntryOnDisk e("");

    int dirid = 0;
    e.id = 0;
    for(unsigned int i=0; i<splitpath.size(); i++)
    {
        dirid = e.id;
        node = OpenNodeInternal(dirid);
        CSimpleFSDirectory(node, *this).Find(splitpath[i], e);
        if (e.id == CFragmentDesc::INVALIDID)
        {
            LOG(LogLevel::DEEP) << "Cannot find node '" << splitpath[i] << "'";
            throw ENOENT; // No such file or directory
        }
        if (i<splitpath.size()-1) assert(node->type == INODETYPE::dir);
    }

    node = OpenNodeInternal(e.id);
    std::lock_guard<std::mutex> lock(node->GetMutex());
    node->parentid = dirid;
    if (splitpath.empty())
        node->name = "/";
    else
        node->name = splitpath.back();

    return node;
}

CSimpleFSDirectoryPtr CSimpleFilesystem::OpenDirInternal(int id)
{
    CSimpleFSInodePtr node = OpenNodeInternal(id);
    // The check whether this is a directory is done in the constructor
    return std::make_shared<CSimpleFSDirectory>(CSimpleFSDirectory(node, *this));
}

CDirectoryPtr CSimpleFilesystem::OpenDir(const std::string &path)
{
    CSimpleFSInodePtr node = OpenNodeInternal(path);
    return std::make_shared<CSimpleFSDirectory>(CSimpleFSDirectory(node, *this));
}

CDirectoryPtr CSimpleFilesystem::OpenDir(const std::vector<std::string> splitpath)
{
    CSimpleFSInodePtr node = OpenNodeInternal(splitpath);
    return std::make_shared<CSimpleFSDirectory>(CSimpleFSDirectory(node, *this));
}

CInodePtr CSimpleFilesystem::OpenFile(int id)
{
    CSimpleFSInodePtr node = OpenNodeInternal(id);
    if (node->type != INODETYPE::file) throw ENOENT;
    if (node->id == CFragmentDesc::INVALIDID) throw ENOENT;
    return node;
}

CInodePtr CSimpleFilesystem::OpenFile(const std::string &path)
{
    CSimpleFSInodePtr node = OpenNodeInternal(path);
    if (node->id == CFragmentDesc::INVALIDID) throw ENOENT;
    if (node->type != INODETYPE::file) throw ENOENT;
    return node;
}

CInodePtr CSimpleFilesystem::OpenFile(const std::vector<std::string> splitpath)
{
    CSimpleFSInodePtr node = OpenNodeInternal(splitpath);
    if (node->id == CFragmentDesc::INVALIDID) throw ENOENT;
    if (node->type != INODETYPE::file) throw ENOENT;
    return node;
}

void CSimpleFilesystem::GrowNode(CSimpleFSInode &node, int64_t size)
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

void CSimpleFilesystem::ShrinkNode(CSimpleFSInode &node, int64_t size)
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

void CSimpleFilesystem::Truncate(CSimpleFSInode &node, int64_t size, bool dozero)
{
    ntruncated++;
    LOG(LogLevel::DEEP) << "Truncate of id=" << node.id << " from: " << node.size << " to: " << size;
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

int64_t CSimpleFilesystem::Read(CSimpleFSInode &node, int8_t *d, int64_t ofs, int64_t size)
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

void CSimpleFilesystem::Write(CSimpleFSInode &node, const int8_t *d, int64_t ofs, int64_t size)
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

void CSimpleFilesystem::Rename(CInodePtr _node, CDirectoryPtr _newdir, const std::string &filename)
{
    CSimpleFSDirectoryPtr newdir = std::dynamic_pointer_cast<CSimpleFSDirectory>(_newdir);
    CSimpleFSInodePtr node = std::dynamic_pointer_cast<CSimpleFSInode>(_node);

    nrenamed++;
    CDirectoryEntryOnDisk e(filename);
    CSimpleFSDirectoryPtr olddir = OpenDirInternal(node->parentid);
    olddir->RemoveEntry(node->name, e);
    strncpy(e.name, filename.c_str(), 64+32);
    newdir->AddEntry(e);
}

int CSimpleFilesystem::CreateNode(CSimpleFSDirectory &dir, const std::string &name, INODETYPE t)
{
    // Reserve one block. Necessary even for empty files
    if (t == INODETYPE::dir) ncreatedir++; else ncreatefiles++;
    int id = fragmentlist.ReserveNewFragment(t);
    bio->Sync();
    if (dir.dirnode->id == CFragmentDesc::INVALIDID) return id; // this is the root directory and does not have a parent
    dir.AddEntry(CDirectoryEntryOnDisk(name, id));
    return id;
}

int CSimpleFilesystem::MakeFile(CSimpleFSDirectory &dir, const std::string &name)
{
    int id = CreateNode(dir, name, INODETYPE::file);
    LOG(LogLevel::DEEP) << "Create File '" << name << "' with id=" << id;
    return id;
}

int CSimpleFilesystem::MakeDirectory(CSimpleFSDirectory &dir, const std::string &name)
{
    int id = CreateNode(dir, name, INODETYPE::dir);
    LOG(LogLevel::DEEP) << "Create Directory '" << name << "' with id=" << id;

    CSimpleFSInodePtr newdirnode = OpenNodeInternal(id);
    newdirnode->parentid = dir.dirnode->id;
    newdirnode->name = name;
    newdirnode->type = INODETYPE::dir;

    CSimpleFSDirectory(newdirnode, *this).Create();
    return id;
}

void CSimpleFilesystem::Remove(CSimpleFSInode &node)
{
    nremoved++;
    // maybe, we have to check the shared_ptr here
    CDirectoryEntryOnDisk e("");
    CSimpleFSDirectoryPtr dir = OpenDirInternal(node.parentid);
    fragmentlist.FreeAllFragments(node.fragments);
    node.fragments.clear();
    dir->RemoveEntry(node.name, e);
    std::lock_guard<std::mutex> lock(inodescachemtx);
    inodes.erase(node.id); // remove from map
}

INODETYPE CSimpleFilesystem::GetType(int32_t id)
{
    return fragmentlist.GetType(id);
}

void CSimpleFilesystem::StatFS(CStatFS *buf)
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

CInodePtr CSimpleFilesystem::OpenNode(int id)
{
    return OpenNodeInternal(id);
}
CInodePtr CSimpleFilesystem::OpenNode(const std::vector<std::string> splitpath)
{
    return OpenNodeInternal(splitpath);
}
CInodePtr CSimpleFilesystem::OpenNode(const std::string& path)
{
    return OpenNodeInternal(path);
}
CDirectoryPtr CSimpleFilesystem::OpenDir(int id)
{
    return OpenDirInternal(id);
}


void CSimpleFilesystem::PrintInfo()
{
    CPrintCheckRepair(*this).PrintInfo();
}

void CSimpleFilesystem::PrintFragments()
{
    CPrintCheckRepair(*this).PrintFragments();
}

void CSimpleFilesystem::Check()
{
    CPrintCheckRepair(*this).Check();
}
