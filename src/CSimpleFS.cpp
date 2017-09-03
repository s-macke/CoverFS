#include "CSimpleFS.h"
#include "CDirectory.h"

#include<stdio.h>
#include<assert.h>
#include<set>
#include<algorithm>
#include<climits>
#include<algorithm>

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

SimpleFilesystem::SimpleFilesystem(CCacheIO &_bio) : bio(_bio), nodeinvalid(new INODE(*this))
{
    nodeinvalid->id = INVALIDID;

    assert(sizeof(DIRENTRY) == 128);
    assert(sizeof(CFragmentDesc) == 16);

    printf("container info:\n");
    printf("  size: %i MB\n", int(bio.GetFilesize()/(1024*1024)));
    printf("  blocksize: %i\n", bio.blocksize);

    CBLOCKPTR superblock = bio.GetBlock(1);
    SUPER* super = (SUPER*)superblock->GetBufRead();
    if (strncmp(super->magic, "CoverFS", 7) == 0)
    {
        printf("filesystem %s V%i.%i\n", super->magic, super->version>>16, super->version|0xFFFF);
        superblock->ReleaseBuf();

        unsigned int nfragmentblocks = 5;
        unsigned int nentries = bio.blocksize*nfragmentblocks/16;
        printf("  number of blocks containing fragments: %i with %i entries\n", nfragmentblocks, nentries);

        fragmentblocks.clear();
        for(unsigned int i=0; i<nfragmentblocks; i++)
            fragmentblocks.push_back(bio.GetBlock(i+2));

        ofssort.assign(nentries, 0);
        for(unsigned int i=0; i<nentries; i++) ofssort[i] = i;

        fragments.assign(nentries, CFragmentDesc(FREEID, 0, 0));

        for(unsigned int i=0; i<nfragmentblocks; i++)
        {
            int nidsperblock = bio.blocksize / 16;
            CBLOCKPTR block = fragmentblocks[i];
            int8_t* buf = block->GetBufRead();
            memcpy(&fragments[i*nidsperblock], buf, sizeof(CFragmentDesc)*nidsperblock);
            block->ReleaseBuf();
        }
        SortOffsets();
        printf("\n");
        return;
    }
    superblock->ReleaseBuf();

    CreateFS();
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

    unsigned int nfragmentblocks = 5;
    unsigned int nentries = bio.blocksize*nfragmentblocks/16;
    printf("  number of blocks containing fragment: %i with %i entries\n", nfragmentblocks, nentries);
// ---
    fragmentblocks.clear();
    for(unsigned int i=0; i<nfragmentblocks; i++)
    {
        fragmentblocks.push_back(bio.GetBlock(i+2));
    }
// ---

    ofssort.assign(nentries, 0);
    for(unsigned int i=0; i<nentries; i++)
    {
        ofssort[i] = i;
    }

    fragments.assign(nentries, CFragmentDesc(FREEID, 0, 0));
    fragments[0] = CFragmentDesc(SUPERID, 0, bio.blocksize*2);
    fragments[1] = CFragmentDesc(TABLEID, 2, bio.blocksize*nfragmentblocks);

    SortOffsets();

// ---

    for(unsigned int i=0; i<nentries; i++)
        StoreFragment(i);

    bio.Sync();
// ---

    // Create root directory

    nodeinvalid->id = INVALIDID;
    nodeinvalid->type = INODETYPE::dir;
    CDirectory rootdir = CDirectory(nodeinvalid, *this);
    int id = rootdir.CreateDirectory(std::string("root"));
    nodeinvalid->id = INVALIDID;
    nodeinvalid->type = INODETYPE::unknown;

    if (id != ROOTID)
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

    printf("Filesystem created\n");
    printf("==================\n");
}

void SimpleFilesystem::StoreFragment(int idx)
{
    int nidsperblock = bio.blocksize / 16;
    CBLOCKPTR block = fragmentblocks[idx/nidsperblock];
    int8_t* buf = block->GetBufReadWrite();
    ((CFragmentDesc*)buf)[idx%nidsperblock] = fragments[idx];
    block->ReleaseBuf();
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
    node->parentid = INVALIDID;

    if (id == ROOTID)
    {
        node->type = INODETYPE::dir;
    }
    fragmentsmtx.lock();
    for(unsigned int i=0; i<fragments.size(); i++)
    {
        if (fragments[i].id != id) continue;
        //printf("OpenNode id=%i: Add fragment with index %i with starting_block=%i size=%i bytes\n", node->id, i, fragments[i].ofs, fragments[i].size);
        node->size += fragments[i].size;
        node->fragments.push_back(i);
    }
    fragmentsmtx.unlock();
    assert(node->fragments.size() > 0);
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
        if (e.id == INVALIDID)
        {
            throw ENOENT;
        }
        if (i<splitpath.size()-1) assert((INODETYPE)e.type == INODETYPE::dir);
    }

    node = OpenNode(e.id);
    std::lock_guard<std::mutex> lock(node->GetMutex());
    node->parentid = dirid;
    node->type = (INODETYPE)e.type; // static cast?
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
    if (node->id == INVALIDID) throw ENOENT;
    return node;
}

INODEPTR SimpleFilesystem::OpenFile(const std::string &path)
{
    INODEPTR node = OpenNode(path);
    if (node->id == INVALIDID) throw ENOENT;
    if (node->type != INODETYPE::file) throw ENOENT;
    return node;
}

INODEPTR SimpleFilesystem::OpenFile(const std::vector<std::string> splitpath)
{
    INODEPTR node = OpenNode(splitpath);
    if (node->id == INVALIDID) throw ENOENT;
    if (node->type != INODETYPE::file) throw ENOENT;
    return node;
}

int SimpleFilesystem::ReserveNextFreeFragment(INODE &node, int64_t maxsize)
{
    assert(node.fragments.size() != 0);

    int lastidx = node.fragments.back();

    //std::lock_guard<std::mutex> lock(fragmentsmtx); // locked elsewhere

    int storeidx = -1;
    // first find a free id
    for(unsigned int i=lastidx+1; i<fragments.size(); i++)
    {
        if (fragments[i].id != FREEID) continue;
        storeidx = i;
        break;
    }
    assert(storeidx != -1); // TODO: change list size in this case

    //printf("  found next free fragment: storeidx=%i\n", storeidx);

    // now search for a big hole
    int idx1=0, idx2=0;
    for(unsigned int i=0; i<ofssort.size()-1; i++)
    {
        idx1 = ofssort[i+0];
        idx2 = ofssort[i+1];

        //printf("  analyze fragment %i with ofsblock=%li size=%u of id=%i\n", idx1, fragments[idx1].ofs, fragments[idx1].size, fragments[idx1].id);
        int64_t nextofs = fragments[idx1].GetNextFreeBlock(bio.blocksize);
        if (fragments[idx2].size == 0) break;
        if (fragments[idx2].id == FREEID) break;

        int64_t hole = (fragments[idx2].ofs  - nextofs)*bio.blocksize;
        assert(hole >= 0);

        // prevent fragmentation
        if ((hole > 0x100000) || (hole > maxsize/4))
        {
            fragments[storeidx].id = node.id;
            fragments[storeidx].size = std::min<int64_t>({maxsize, hole, (int64_t)0xFFFFFFFFL});
            fragments[storeidx].ofs = nextofs;
            return storeidx;
        }
    }

    // No hole found, so put it at the end
    //printf("no hole found\n");
    fragments[storeidx].id = node.id;
    fragments[storeidx].size = std::min<int64_t>(maxsize, 0xFFFFFFFFL);
    if (fragments[idx1].size == 0)
        fragments[storeidx].ofs = fragments[idx1].ofs;
    else
        fragments[storeidx].ofs = fragments[idx1].ofs + (fragments[idx1].size-1)/bio.blocksize + 1;

    return storeidx;
}

void SimpleFilesystem::SortOffsets()
{
    std::sort(ofssort.begin(),ofssort.end(), [&](int a, int b)
    {
        int ofs1 = fragments[a].ofs;
        int ofs2 = fragments[b].ofs;
        if (fragments[a].size == 0) ofs1 = INT_MAX;
        if (fragments[b].size == 0) ofs2 = INT_MAX;
        if (fragments[a].id == FREEID) ofs1 = INT_MAX;
        if (fragments[b].id == FREEID) ofs2 = INT_MAX;
        return ofs1 < ofs2;
    });
}


void SimpleFilesystem::GrowNode(INODE &node, int64_t size)
{
    std::lock_guard<std::mutex> lock(fragmentsmtx);
    while(node.size < size)
    {
        int storeidx = ReserveNextFreeFragment(node, size-node.size);
        assert(node.fragments.back() != storeidx);
        CFragmentDesc &fd = fragments[storeidx];

        uint64_t nextofs = fragments[node.fragments.back()].GetNextFreeBlock(bio.blocksize);
        if (fragments[node.fragments.back()].size == 0) // empty fragment can be overwritten
        {
            storeidx = node.fragments.back();
            fragments[storeidx] = fd;
            fd.id = FREEID;
        } else
        if (nextofs == fd.ofs) // merge
        {
            storeidx = node.fragments.back();

            if (node.size+(int64_t)fd.size > 0xFFFFFFFFL)
            {
                exit(1);
                // TODO: check for 4GB bouondary
            }
            fragments[storeidx].size += fd.size;
            fd.id = FREEID;
        } else
        {
            node.fragments.push_back(storeidx);
        }

        node.size += fd.size;
        StoreFragment(storeidx);
        SortOffsets();
    }

}


void SimpleFilesystem::ShrinkNode(INODE &node, int64_t size)
{
    std::lock_guard<std::mutex> lock(fragmentsmtx); // not interfere with sorted offset list
    while(node.size > 0)
    {
        int lastidx = node.fragments.back();
        CFragmentDesc &r = fragments[lastidx];
        node.size -= r.size;
        r.size = std::max(size - node.size, (int64_t)0);
        node.size += r.size;

        if ((r.size == 0) && (node.size != 0)) // don't remove last element
        {
            r.id = FREEID;
            StoreFragment(lastidx);
            node.fragments.pop_back();
        } else
        {
            StoreFragment(lastidx);
            break;
        }
    }
    SortOffsets();
}

void SimpleFilesystem::Truncate(INODE &node, int64_t size, bool dozero)
{
    //printf("Truncate of id=%i to:%li from:%li\n", node.id, size, node.size);
    assert(node.id != INVALIDID);
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
            if (FindIntersect(CFragmentOverlap(fragmentofs, fragments[idx].size), CFragmentOverlap(ofs, size-ofs), intersect))
            {
                assert(intersect.ofs >= ofs);
                assert(intersect.ofs >= fragmentofs);
                bio.Zero(
                    fragments[idx].ofs*bio.blocksize + (intersect.ofs - fragmentofs),
                    intersect.size);
            }
            fragmentofs += fragments[idx].size;
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
        assert(fragments[idx].id == node.id);
        CFragmentOverlap intersect;
        if (FindIntersect(CFragmentOverlap(fragmentofs, fragments[idx].size), CFragmentOverlap(ofs, size), intersect))
        {
            assert(intersect.ofs >= ofs);
            assert(intersect.ofs >= fragmentofs);
            bio.Read(
                fragments[idx].ofs*bio.blocksize + (intersect.ofs - fragmentofs),
                intersect.size,
                &d[intersect.ofs-ofs]);
            s += intersect.size;
        }
        fragmentofs += fragments[idx].size;
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
        if (FindIntersect(CFragmentOverlap(fragmentofs, fragments[idx].size), CFragmentOverlap(ofs, size), intersect))
        {
            assert(intersect.ofs >= ofs);
            assert(intersect.ofs >= fragmentofs);
            bio.Write(
                fragments[idx].ofs*bio.blocksize + (intersect.ofs - fragmentofs),
                intersect.size,
                &d[intersect.ofs-ofs]);
        }
        fragmentofs += fragments[idx].size;
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
    if (e.id != INVALIDID)
    {
    }
    */
    newdir.AddEntry(e);
}


int SimpleFilesystem::ReserveNewFragment()
{
    std::lock_guard<std::mutex> lock(fragmentsmtx);

    int idmax = -1;
    for(unsigned int i=0; i<fragments.size(); i++)
    {
        if (fragments[i].id > idmax) idmax = fragments[i].id;
    }
    int id = idmax+1;
    //printf("get free id %i\n", id);

    for(unsigned int i=0; i<fragments.size(); i++)
    {
        if (fragments[i].id != FREEID) continue;
        fragments[i] = CFragmentDesc(id, 0, 0);
        StoreFragment(i);
        // SortOffsets(); // Sorting is not necessary, because a FREEID and ofs=0 are treated the same way
        return id;
    }
    fprintf(stderr, "Error: No free fragments available\n");
    exit(1);
    return id;
}

int SimpleFilesystem::CreateNode(CDirectory &dir, const std::string &name, INODETYPE t)
{
    // Reserve one block. Necessary even for empty files
    int id = ReserveNewFragment();
    bio.Sync();
    if (dir.dirnode->id == INVALIDID) return id; // this is the root directory and does not have a parent
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

void SimpleFilesystem::FreeAllFragments(INODE &node)
{
    std::lock_guard<std::mutex> lock(fragmentsmtx);
    for(unsigned int i=0; i<node.fragments.size(); i++)
    {
        fragments[node.fragments[i]].id = FREEID;
        StoreFragment(node.fragments[i]);
    }
    SortOffsets();
    node.fragments.clear();
    bio.Sync();
}

void SimpleFilesystem::Remove(INODE &node)
{
    //maybe, we have to check the shared_ptr here
    DIRENTRY e("");
    CDirectory dir = OpenDir(node.parentid);
    FreeAllFragments(node);
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

    std::lock_guard<std::mutex> lock(fragmentsmtx);

    std::set<int32_t> s;
    int64_t size=0;
    for(unsigned int i=0; i<fragments.size(); i++)
    {
        int32_t id = fragments[i].id;
        if (id >= 0)
	{
		size += fragments[i].size/bio.blocksize+1;
		s.insert(id);
	}
    }
    buf->f_bfree  = totalsize / bio.blocksize - size;
    buf->f_bavail = totalsize / bio.blocksize - size;
    buf->f_files  = s.size();
}
