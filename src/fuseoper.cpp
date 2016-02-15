#include<stdio.h>
#include<string.h>
#include<errno.h>
#include<assert.h>
#include"CSimpleFS.h"
#include"CDirectory.h"

#include"fuseoper.h"

//http://fuse.sourceforge.net/doxygen/fusexmp__fh_8c.html
//https://www.cs.hmc.edu/~geoff/classes/hmc.cs135.201001/homework/fuse/fuse_doc.html

#define FUSE_USE_VERSION 26
extern "C" {
    #include <fuse.h>
}

static SimpleFilesystem *fs;

static int fuse_getattr(const char *path, struct stat *stbuf)
{
    printf("getattr '%s'\n", path);

    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    try
    {
        INODEPTR node = fs->OpenNode(path);
        stbuf->st_size = node->size;
        stbuf->st_blocks = node->size/512;
        stbuf->st_nlink = 1;
        if (node->type == INODETYPE::dir)
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
    printf("utimens '%s'\n", path);
    return 0;
}

static int fuse_chmod(const char *path, mode_t mode)
{
    printf("chmod '%s'\n", path);
    return 0;
}

static int fuse_chown(const char *path, uid_t uid, gid_t gid)
{
    printf("chown '%s'\n", path);
    return 0;
}

static int fuse_truncate(const char *path, off_t size)
{
    printf("truncate '%s' size=%li\n", path, size);
    try
    {
        INODEPTR node = fs->OpenFile(path);
        node->Truncate(size);
    } catch(const int &err)
    {
        return -err;
    }
    return 0;
}

static int fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;
    printf("readdir '%s'\n", path);
    try
    {
        CDirectory dir = fs->OpenDir(path);
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);

        dir.ForEachEntry([&](DIRENTRY &de)
        {
            if ((INODETYPE)de.type == INODETYPE::free) return FOREACHENTRYRET::OK;
            filler(buf, de.name, NULL, 0);
            return FOREACHENTRYRET::OK;
        });
    } catch(const int &err)
    {
        return -err;
    }

    return 0;
}

static int fuse_open(const char *path, struct fuse_file_info *fi)
{
    printf("open '%s'\n", path);
    try
    {
        INODEPTR node = fs->OpenFile(path);
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
    printf("read '%s' ofs=%li size=%li\n", path, offset, size);
    (void) fi;
    try
    {
        INODEPTR node = fs->OpenFile(path);
        size = node->Read((int8_t*)buf, offset, size);
    } catch(const int &err)
    {
        return -err;
    }

    return size;
}

static int fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    printf("write '%s' ofs=%li size=%li\n", path, offset, size);

    (void) fi;
    try
    {
        INODEPTR node = fs->OpenFile(path);
        node->Write((int8_t*)buf, offset, size);
    } catch(const int &err)
    {
        return -err;
    }

    return size;
}


static int fuse_mkdir(const char *path, mode_t mode)
{
    printf("mkdir '%s'\n", path);
    // test if dir is empty? Looks like this is tested already
    std::vector<std::string> splitpath;
    splitpath = SplitPath(std::string(path));
    assert(splitpath.size() >= 1);
    std::string dirname = splitpath.back();
    splitpath.pop_back();
    try
    {
        CDirectory dir = fs->OpenDir(splitpath);
        dir.CreateDirectory(dirname);
    } catch(const int &err)
    {
        return -err;
    }
    return 0;
}

static int fuse_rmdir(const char *path)
{
    printf("rmdir '%s'\n", path);

    try
    {
        CDirectory dir = fs->OpenDir(path);
        if (!dir.IsEmpty()) return -ENOTEMPTY;
        dir.dirnode->Remove();
    } catch(const int &err)
    {
        return -err;
    }
    return 0;
}

static int fuse_unlink(const char *path)
{
    printf("unlink '%s'\n", path);

    try
    {
        INODEPTR node = fs->OpenNode(path);
        node->Remove();
    } catch(const int &err)
    {
        return -err;
    }

    return 0;
}

static int fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    printf("create '%s'\n", path);
    std::vector<std::string> splitpath;
    splitpath = SplitPath(std::string(path));
    assert(splitpath.size() >= 1);
    std::string filename = splitpath.back();
    splitpath.pop_back();
    try
    {
        CDirectory dir = fs->OpenDir(splitpath);
        dir.CreateFile(filename);
    } catch(const int &err)
    {
        return -err;
    }

    return 0;
}

static int fuse_access(const char *path, int mask)
{
    printf("access '%s'\n", path);
    try
    {
        INODEPTR node = fs->OpenNode(path);
    } catch(const int &err)
    {
        return -err;
    }

    return 0;
}

static int fuse_rename(const char *oldpath, const char *newpath)
{
    printf("rename '%s' to '%s'\n", oldpath, newpath);

    std::vector<std::string> splitpath;
    splitpath = SplitPath(std::string(newpath));
    assert(splitpath.size() >= 1);

    try
    {
        INODEPTR newnode = fs->OpenNode(splitpath);
        return -EEXIST;
    }
    catch(...){}

    try
    {
        INODEPTR node = fs->OpenNode(oldpath);
        
        std::string filename = splitpath.back();
        splitpath.pop_back();
        CDirectory dir = fs->OpenDir(splitpath);
        fs->Rename(node, dir, filename); // TODO: check if rename overwrites an already existing file.
    } catch(const int &err)
    {
        return -err;
    }        
    return 0;
}

static int fuse_statfs(const char *path, struct statvfs *buf)
{
    printf("statfs '%s'\n", path);
    fs->StatFS(buf);
    return 0;
}




int StartFuse(int argc, char *argv[], const char* mountpoint, SimpleFilesystem &_fs)
{
    fs = &_fs;

    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);

    fuse_opt_add_arg(&args, "-odirect_io");
    fuse_opt_add_arg(&args, "-obig_writes");
    fuse_opt_add_arg(&args, "-oasync_read");
    fuse_opt_add_arg(&args, "-f");
    fuse_opt_add_arg(&args, mountpoint);

    struct fuse_operations fuse_oper;
    memset(&fuse_oper, 0, sizeof(struct fuse_operations));
    fuse_oper.getattr     = fuse_getattr;
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

    return fuse_main(args.argc, args.argv, &fuse_oper, NULL);
}

