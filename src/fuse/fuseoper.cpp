#include<cstdio>
#include<cstring>
#include<cerrno>
#include<cassert>
#include<vector>

#include"Logger.h"
#include"../FS/CFilesystem.h"

#include"fuseoper.h"

//http://fuse.sourceforge.net/doxygen/fusexmp__fh_8c.html
//https://www.cs.hmc.edu/~geoff/classes/hmc.cs135.201001/homework/fuse/fuse_doc.html

#define FUSE_USE_VERSION 26
extern "C" {
    #include <fuse.h>
}

static CFilesystem *fs;
std::string mountpoint;
struct fuse *fusectx = NULL;
struct fuse_chan *fuse_chan = NULL;

static int fuse_getattr(const char *path, struct stat *stbuf)
{
    LOG(LogLevel::INFO) << "FUSE: getattr '" << path << "'";

    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    try
    {
        CInodePtr node = fs->OpenNode(path);
        int64_t size = node->GetSize();
        stbuf->st_size = size;
        stbuf->st_blocks = size/512;
        stbuf->st_nlink = 1;
        if (node->GetType() == INODETYPE::dir)
        {
            stbuf->st_mode = S_IFDIR | 0755;
        } else
        {
            stbuf->st_mode = S_IFREG | 0666;
        }

    } catch(const int &err)
    {
        return -err;
    }

    return 0;
}

static int fuse_utimens(const char *path, const struct timespec tv[2])
{
    LOG(LogLevel::INFO) << "FUSE: utimens '" << path << "'";
    return 0;
}

static int fuse_chmod(const char *path, mode_t mode)
{
    LOG(LogLevel::INFO) << "FUSE: chmod '" << path << "'";
    return 0;
}

static int fuse_chown(const char *path, uid_t uid, gid_t gid)
{
    LOG(LogLevel::INFO) << "FUSE: chown '" << path << "'";
    return 0;
}

static int fuse_truncate(const char *path, off_t size)
{
    LOG(LogLevel::INFO) << "FUSE: truncate '" << path << "' size=" << size;
    try
    {
        CInodePtr node = fs->OpenFile(path);
        node->Truncate(size, true);
    } catch(const int &err)
    {
        return -err;
    }
    return 0;
}

static int fuse_opendir(const char *path, struct fuse_file_info *fi)
{
    LOG(LogLevel::INFO) << "FUSE: opendir '" << path << "'";
    try
    {
        CDirectoryPtr dir = fs->OpenDir(path);
        fi->fh = dir->GetId();
    } catch(const int &err)
    {
        return -err;
    }
    return 0;
}

static int fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    LOG(LogLevel::INFO) << "FUSE: readdir '" << path << "'";
    try
    {
        CDirectoryPtr dir = fs->OpenDir(fi->fh);
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        CDirectoryIteratorPtr iterator = dir->GetIterator();
        while(iterator->HasNext())
        {
            filler(buf, iterator->Next().name.c_str(), NULL, 0);
        }
    } catch(const int &err)
    {
        return -err;
    }

    return 0;
}

static int fuse_open(const char *path, struct fuse_file_info *fi)
{
    LOG(LogLevel::INFO) << "FUSE: open '" << path << "'";
    try
    {
        CInodePtr node = fs->OpenFile(path);
        fi->fh = node->GetId();
    } catch(const int &err)
    {
        return -err;
    }
    /*
        if ((fi->flags & 3) != O_RDONLY)
                return -EACCES;
    */
    return 0;
}

static int fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    LOG(LogLevel::INFO) << "FUSE: read '" << path << "' ofs=" << offset << " size=" << size;
    try
    {
        CInodePtr node = fs->OpenFile(fi->fh);
        size = node->Read((int8_t*)buf, offset, size);
    } catch(const int &err)
    {
        return -err;
    }

    return size;
}

static int fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    LOG(LogLevel::INFO) << "FUSE: write '" << path << "' ofs=" << offset << " size=" << size;

    try
    {
        CInodePtr node = fs->OpenFile(fi->fh);
        node->Write((int8_t*)buf, offset, size);
    } catch(const int &err)
    {
        return -err;
    }
    return size;
}


static int fuse_mkdir(const char *path, mode_t mode)
{
    LOG(LogLevel::INFO) << "FUSE: mkdir '" << path << "'";
    // test if dir is empty? Looks like this is tested already
    std::vector<std::string> splitpath;
    splitpath = SplitPath(std::string(path));
    assert(splitpath.size() >= 1);
    std::string dirname = splitpath.back();
    splitpath.pop_back();
    try
    {
        CDirectoryPtr dir = fs->OpenDir(splitpath);
        dir->MakeDirectory(dirname);
    } catch(const int &err)
    {
        return -err;
    }
    return 0;
}

static int fuse_rmdir(const char *path)
{
    LOG(LogLevel::INFO) << "FUSE: rmdir '" << path << "'";

    try
    {
        CDirectoryPtr dir = fs->OpenDir(path);
        if (!dir->IsEmpty()) return -ENOTEMPTY;
        dir->Remove();
    } catch(const int &err)
    {
        return -err;
    }
    return 0;
}

static int fuse_unlink(const char *path)
{
    LOG(LogLevel::INFO) << "FUSE: unlink '" << path << "'";

    try
    {
        CInodePtr node = fs->OpenNode(path);
        node->Remove();
    } catch(const int &err)
    {
        return -err;
    }

    return 0;
}

static int fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    LOG(LogLevel::INFO) << "FUSE: create '" << path << "'";
    std::vector<std::string> splitpath;
    splitpath = SplitPath(std::string(path));
    assert(splitpath.size() >= 1);
    std::string filename = splitpath.back();
    splitpath.pop_back();
    try
    {
        CDirectoryPtr dir = fs->OpenDir(splitpath);
        fi->fh = dir->MakeFile(filename);
    } catch(const int &err)
    {
        return -err;
    }

    return 0;
}

static int fuse_access(const char *path, int mask)
{
    LOG(LogLevel::INFO) << "FUSE: access '" << path << "'";
    try
    {
        CInodePtr node = fs->OpenNode(path);
    } catch(const int &err)
    {
        return -err;
    }

    return 0;
}

static int fuse_rename(const char *oldpath, const char *newpath)
{
    LOG(LogLevel::INFO) << "FUSE: create '" << oldpath << "' to '" << newpath << "'";

    std::vector<std::string> splitpath;
    splitpath = SplitPath(std::string(newpath));
    assert(!splitpath.empty());

    try
    {
        CInodePtr newnode = fs->OpenNode(splitpath);
        return -EEXIST;
    }
    catch(...){}

    try
    {
        CInodePtr node = fs->OpenNode(oldpath);

        std::string filename = splitpath.back();
        splitpath.pop_back();
        CDirectoryPtr dir = fs->OpenDir(splitpath);
        fs->Rename(node, dir, filename); // TODO: check if rename overwrites an already existing file.
    } catch(const int &err)
    {
        return -err;
    }
    return 0;
}

static int fuse_statfs(const char *path, struct statvfs *buf)
{
    LOG(LogLevel::INFO) << "FUSE: statfs '" << path << "'";
    CStatFS stat;
    fs->StatFS(&stat);
    buf->f_bavail = stat.f_bavail;
    buf->f_bfree = stat.f_bfree;
    buf->f_blocks = stat.f_blocks;
    buf->f_bsize = stat.f_bsize;
    buf->f_files = stat.f_files;
    buf->f_frsize = stat.f_frsize;
    buf->f_namemax = stat.f_namemax;

    return 0;
}

int StopFuse()
{
    if (fs == NULL) return EXIT_SUCCESS;
    if (fusectx == NULL) return EXIT_SUCCESS;
    LOG(LogLevel::INFO) << "Unmount FUSE mountpoint: " << mountpoint;
    fuse_unmount(mountpoint.c_str(), fuse_chan);
    return EXIT_SUCCESS;
}

int StartFuse(int argc, char *argv[], const char* _mountpoint, CFilesystem &_fs)
{
    fs = &_fs;
    mountpoint = std::string(_mountpoint);

    LOG(LogLevel::INFO) << "FUSE Version: " << fuse_version();

    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);

    fuse_opt_add_arg(&args, "-odirect_io");
    fuse_opt_add_arg(&args, "-obig_writes");
    fuse_opt_add_arg(&args, "-oasync_read");
//    fuse_opt_add_arg(&args, "-f");
//    fuse_opt_add_arg(&args, _mountpoint);

    struct fuse_operations fuse_oper;
    memset(&fuse_oper, 0, sizeof(struct fuse_operations));
    fuse_oper.getattr     = fuse_getattr;
    fuse_oper.opendir     = fuse_opendir;
    fuse_oper.readdir     = fuse_readdir;
    fuse_oper.open        = fuse_open;
    fuse_oper.read        = fuse_read;
    fuse_oper.write       = fuse_write;
    fuse_oper.mkdir       = fuse_mkdir;
    fuse_oper.create      = fuse_create;
    fuse_oper.rmdir       = fuse_rmdir;
    fuse_oper.unlink      = fuse_unlink;
    fuse_oper.truncate    = fuse_truncate;
    fuse_oper.access      = fuse_access;
    fuse_oper.rename      = fuse_rename;
    fuse_oper.chmod       = fuse_chmod;
    fuse_oper.chown       = fuse_chown;
    fuse_oper.statfs      = fuse_statfs;
    fuse_oper.utimens     = fuse_utimens;
    //fuse_oper.flag_nullpath_ok = 1;

    fuse_chan = fuse_mount(_mountpoint, &args);
    fusectx = fuse_new(fuse_chan, &args, &fuse_oper, sizeof(fuse_oper), NULL);

    int ret =  fuse_loop_mt(fusectx);
    fuse_destroy(fusectx);
    return ret;

//    return fuse_loop(fusectx);
//    return fuse_main(args.argc, args.argv, &fuse_oper, NULL);
}

