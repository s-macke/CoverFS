#include<climits>
#include<cassert>
#include<algorithm>

#include"Logger.h"
#include"CFragment.h"

void CFragmentList::Load()
{
    unsigned int nfragmentblocks = 5;
    unsigned int nentries = bio->blocksize*nfragmentblocks/CFragmentDesc::SIZEONDISK;
    LOG(LogLevel::INFO) << "  number of blocks containing fragments: " << nfragmentblocks << " with " << nentries << " entries";

    fragmentblocks.clear();
    for(unsigned int i=0; i<nfragmentblocks; i++)
        fragmentblocks.push_back(bio->GetBlock(i+2));

    ofssort.assign(nentries, 0);
    for(unsigned int i=0; i<nentries; i++) ofssort[i] = i;

    fragments.assign(nentries, CFragmentDesc(INODETYPE::undefined, CFragmentDesc::FREEID, 0, 0));

    for(unsigned int i=0; i<nfragmentblocks; i++)
    {
        int nidsperblock = bio->blocksize / CFragmentDesc::SIZEONDISK;
        CBLOCKPTR block = fragmentblocks[i];
        int8_t* buf = block->GetBufRead();
        for(int j=0; j<nidsperblock; j++)
        {
            fragments[i*nidsperblock + j] = CFragmentDesc(&buf[j*CFragmentDesc::SIZEONDISK]);
        }
        block->ReleaseBuf();
    }
    SortOffsets();
}

void CFragmentList::Create()
{
    unsigned int nfragmentblocks = 5;
    unsigned int nentries = bio->blocksize*nfragmentblocks/16;
    LOG(LogLevel::INFO) << "  number of blocks containing fragments: " << nfragmentblocks << " with " << nentries << " entries";
// ---
    fragmentblocks.clear();
    for(unsigned int i=0; i<nfragmentblocks; i++)
    {
        fragmentblocks.push_back(bio->GetBlock(i+2));
    }
// ---

    ofssort.assign(nentries, 0);
    for(unsigned int i=0; i<nentries; i++)
    {
        ofssort[i] = i;
    }

    fragments.assign(nentries, CFragmentDesc(INODETYPE::undefined, CFragmentDesc::FREEID, 0, 0));
    fragments[0] = CFragmentDesc(INODETYPE::special, CFragmentDesc::SUPERID, 0, bio->blocksize*2);
    fragments[1] = CFragmentDesc(INODETYPE::special, CFragmentDesc::TABLEID, 2, bio->blocksize*nfragmentblocks);

    SortOffsets();

    for(unsigned int i=0; i<nentries; i++)
        StoreFragment(i);

    bio->Sync();
}


void CFragmentList::StoreFragment(int idx)
{
    int nidsperblock = bio->blocksize / 16;
    CBLOCKPTR block = fragmentblocks[idx/nidsperblock];
    int8_t* buf = block->GetBufReadWrite();
    fragments[idx].ToDisk( &buf[(idx%nidsperblock) * CFragmentDesc::SIZEONDISK] );
    block->ReleaseBuf();
}

void CFragmentList::FreeAllFragments(std::vector<int> &ff)
{
    std::lock_guard<std::mutex> lock(fragmentsmtx);
    for (int f : ff)
    {
        fragments[f].id = CFragmentDesc::FREEID;
        fragments[f].type = INODETYPE::undefined;
        StoreFragment(f);
    }
    SortOffsets();
    bio->Sync();
}

void CFragmentList::GetFragmentIdxList(int32_t id, std::vector<int> &list, int64_t &size)
{
    size = 0;
    list.clear();
    std::lock_guard<std::mutex> lock(fragmentsmtx);
    for(unsigned int i=0; i<fragments.size(); i++)
    {
        if (fragments[i].id != id) continue;
        size += fragments[i].size;
        list.push_back(i);
    }
}

INODETYPE CFragmentList::GetType(int32_t id)
{
    std::lock_guard<std::mutex> lock(fragmentsmtx);
    for (auto &fragment : fragments)
    {
        if (fragment.id != id) continue;
        return fragment.type;
    }
    return INODETYPE::undefined;
}


int CFragmentList::ReserveNewFragment(INODETYPE type)
{
    std::lock_guard<std::mutex> lock(fragmentsmtx);

    int idmax = -1;
    for (auto &fragment : fragments)
    {
        if (fragment.id > idmax) idmax = fragment.id;
    }
    int id = idmax+1;
    LOG(LogLevel::DEEP) << "Reserve new id " << id << " of type " << (int)type;

    for(unsigned int i=0; i<fragments.size(); i++)
    {
        if (fragments[i].id != CFragmentDesc::FREEID) continue;
        fragments[i] = CFragmentDesc(type, id, 0, 0);
        StoreFragment(i);
        //SortOffsets(); // Sorting is not necessary, because a FREEID and ofs=0 are treated the same way
        return id;
    }
    LOG(LogLevel::ERR) << "No free fragments available\n";
    exit(1);
    return id;
}


int CFragmentList::ReserveNextFreeFragment(int lastidx, int32_t id, INODETYPE type, int64_t maxsize)
{
    LOG(LogLevel::DEEP) << "Reserve next free fragment " << id;
    //assert(node.fragments.size() != 0);
    //int lastidx = node.fragments.back();

    //std::lock_guard<std::mutex> lock(fragmentsmtx); // locked elsewhere

    int storeidx = -1;
    // first find a free id
    for(unsigned int i=lastidx+1; i<fragments.size(); i++)
    {
        if (fragments[i].id != CFragmentDesc::FREEID) continue;
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
        int64_t nextofs = fragments[idx1].GetNextFreeBlock(bio->blocksize);
        if (fragments[idx2].size == 0) break;
        if (fragments[idx2].id == CFragmentDesc::FREEID) break;

        int64_t hole = (fragments[idx2].ofs  - nextofs)*bio->blocksize;
        assert(hole >= 0);

        // prevent fragmentation
        if ((hole > 0x100000) || (hole > maxsize/4))
        {
            fragments[storeidx].id = id;
            fragments[storeidx].size = std::min<int64_t>({maxsize, hole, (int64_t)0xFFFFFFFFL});
            fragments[storeidx].ofs = nextofs;
            fragments[storeidx].type = type;
            return storeidx;
        }
    }

    // No hole found, so put it at the end
    //printf("no hole found\n");
    fragments[storeidx].id = id;
    fragments[storeidx].size = std::min<int64_t>(maxsize, 0xFFFFFFFFL);
    fragments[storeidx].type = type;
    if (fragments[idx1].size == 0)
        fragments[storeidx].ofs = fragments[idx1].ofs;
    else
        fragments[storeidx].ofs = fragments[idx1].ofs + (fragments[idx1].size-1)/bio->blocksize + 1;

    return storeidx;
}

void CFragmentList::SortOffsets()
{
    std::sort(ofssort.begin(),ofssort.end(), [&](int a, int b)
    {
        int ofs1 = fragments[a].ofs;
        int ofs2 = fragments[b].ofs;
        if (fragments[a].size == 0) ofs1 = INT_MAX;
        if (fragments[b].size == 0) ofs2 = INT_MAX;
        if (fragments[a].id == CFragmentDesc::FREEID) ofs1 = INT_MAX;
        if (fragments[b].id == CFragmentDesc::FREEID) ofs2 = INT_MAX;
        return ofs1 < ofs2;
    });
}




