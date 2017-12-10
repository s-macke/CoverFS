#include<set>
#include<algorithm>
#include<climits>

#include"CPrintCheckRepair.h"
#include"CDirectory.h"

void CPrintCheckRepair::GetRecursiveDirectories(std::map<int32_t, std::string> &direntries, int id, const std::string &path)
{
    //printf("recursive: %i %li\n", id, fs.GetNInodes());
    std::string newpath;
    try
    {
        INODEPTR node = fs.OpenNode(id);
        //printf("recursive opened: %i\n", id);
        CDirectory dir = CDirectory(node, fs);

        dir.ForEachEntry([&](DIRENTRY &de)
        {
            //printf("id=%9i: '%s/%s'\n", de.id, path.c_str(), de.name);
            if (de.id == CFragmentDesc::INVALIDID) return FOREACHENTRYRET::OK;
            auto it = direntries.find(de.id);
            if (it != direntries.end())
            {
                printf("Warning: Found two directory entries with the same id id=%i in directory '%s' and directory '%s'\n",
                de.id, path.c_str(), it->second.c_str());
            }
            direntries[de.id] = path + "/" + de.name;
            if (fs.fragmentlist.GetType(de.id) == INODETYPE::dir)
            {
                newpath = path + "/" + de.name;
                GetRecursiveDirectories(direntries, de.id, newpath);
            }
            return FOREACHENTRYRET::OK;
        });
    }
    catch(const int &err)
    {
        printf("Error: Cannot open dir '%s' with id=%i. Errno: %s\n", path.c_str(), id, strerror(err));
    }
}

void  CPrintCheckRepair::PrintFragments()
{
    auto &fragments = fs.fragmentlist.fragments;

    printf("Receive List of all directories\n");
    std::map<int32_t, std::string> direntries;
    GetRecursiveDirectories(direntries, 0, "");

    printf("Fragment List:\n");
    for(unsigned int i=0; i<fs.fragmentlist.ofssort.size(); i++)
    {
        //int idx1 = ofssort[i];
        int idx1 = i;
        if (fragments[idx1].id == CFragmentDesc::FREEID) continue;
        printf("frag=%6i type=%2i id=%6i ofs=%7llu size=%10llu '%s'\n",
            idx1,
            (int)fragments[idx1].type,
            fragments[idx1].id,
            (long long unsigned int)fragments[idx1].ofs,
            (long long unsigned int)fragments[idx1].size,
            direntries[fragments[idx1].id].c_str());
        }
}

void  CPrintCheckRepair::Check()
{
    // check for overlap
    fs.fragmentlist.SortOffsets();

    printf("Check for overlap\n");
    int idx1, idx2;
    for(unsigned int i=0; i<fs.fragmentlist.ofssort.size()-1; i++)
    {
        idx1 = fs.fragmentlist.ofssort[i+0];
        idx2 = fs.fragmentlist.ofssort[i+1];

        int nextofs = fs.fragmentlist.fragments[idx1].GetNextFreeBlock(fs.bio->blocksize);
        if (fs.fragmentlist.fragments[idx2].size == 0) break;
        if (fs.fragmentlist.fragments[idx2].id == CFragmentDesc::FREEID) break;
        int64_t hole = (fs.fragmentlist.fragments[idx2].ofs  - nextofs)*fs.bio->blocksize;
        if (hole < 0)
        {
            fprintf(stderr, "Error in CheckFS: fragment overlap detected");
            exit(1);
        }
    }

    printf("Check for different types in fragments\n");
    std::map<int32_t, INODETYPE> mapping;
    for(unsigned int i=0; i<fs.fragmentlist.fragments.size(); i++)
    {
        int32_t id = fs.fragmentlist.fragments[i].id;

        auto it = mapping.find(id);
        if (it != mapping.end())
        {
            if (it->second != fs.fragmentlist.fragments[i].type)
            {
                printf("Fragment %i Type mismatch for node id=%i: type %i vs %i\n", i, id, (int)it->second, (int)fs.fragmentlist.fragments[i].type);
            }
        } else
        {
            mapping[id] = fs.fragmentlist.fragments[i].type;
        }
    }

    printf("Receive List of all directories\n");
    std::map<int32_t, std::string> direntries;
    GetRecursiveDirectories(direntries, 0, "");
}

void  CPrintCheckRepair::PrintInfo()
{
    fs.fragmentlist.SortOffsets();

    std::set<int32_t> s;
    std::map<INODETYPE, int32_t> types;
    int64_t size=0;
    uint64_t lastfreeblock = 0;
    for(unsigned int i=0; i<fs.fragmentlist.fragments.size(); i++)
    {
        int32_t id = fs.fragmentlist.fragments[i].id;
        if (id >= 0)
        {
                size += fs.fragmentlist.fragments[i].size;
                if (lastfreeblock < fs.fragmentlist.fragments[i].GetNextFreeBlock(fs.bio->blocksize))
                    lastfreeblock = fs.fragmentlist.fragments[i].GetNextFreeBlock(fs.bio->blocksize);
                s.insert(id);
                types[fs.fragmentlist.fragments[i].type]++;
        }
    }
    printf("number of inodes: %zu\n", s.size());
    printf("stored bytes: %lli\n", (long long int)size);
    printf("container usage: %f %%\n", (double)size/(double)fs.bio->GetFilesize()*100.);
    printf("last free block: %lli\n", (long long int)lastfreeblock);
    printf("empty space at end: %lli Bytes\n", (long long int)fs.bio->GetFilesize()-lastfreeblock*fs.bio->blocksize);

    printf("directory fragments: %i\n", types[INODETYPE::dir]);
    printf("     file fragments: %i\n", types[INODETYPE::file]);

    printf("Fragmentation:\n");

    int frags[8] = {0};
    for(auto f : s)
    {
        int nfragments = 0;
        for(unsigned int i=0; i<fs.fragmentlist.fragments.size(); i++)
        {
            if (fs.fragmentlist.fragments[i].id == f) nfragments++;
        }
        int idx = 0;
        if (nfragments > 20) idx = 7; else
        if (nfragments > 10) idx = 6; else
        if (nfragments > 5) idx = 5; else
        if (nfragments > 4) idx = 4; else
        if (nfragments > 3) idx = 3; else
        if (nfragments > 2) idx = 2; else
        if (nfragments > 1) idx = 1; else
        if (nfragments > 0) idx = 0;
        frags[idx]++;
    }

    printf("  inodes with 1   fragment : %4i\n", frags[0]);
    printf("  inodes with 2   fragments: %4i\n", frags[1]);
    printf("  inodes with 3   fragments: %4i\n", frags[2]);
    printf("  inodes with 4   fragments: %4i\n", frags[3]);
    printf("  inodes with 5   fragments: %4i\n", frags[4]);
    printf("  inodes with >5  fragments: %4i\n", frags[5]);
    printf("  inodes with >10 fragments: %4i\n", frags[6]);
    printf("  inodes with >20 fragments: %4i\n", frags[7]);
}

