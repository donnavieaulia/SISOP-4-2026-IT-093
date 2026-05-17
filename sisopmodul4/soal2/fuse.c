#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>

static const char *base_path = "encrypted_storage";

void xor_crypt(char *data, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        data[i] ^= 0x76;
    }
}

void fullpath(char fpath[1024], const char *path)
{
    sprintf(fpath, "%s%s.enc", base_path, path);
}

static int x_getattr(const char *path, struct stat *stbuf)
{
    char fpath[1024];

    memset(stbuf, 0, sizeof(struct stat));

    // root directory
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
        return 0;
    }

    fullpath(fpath, path);

    if (lstat(fpath, stbuf) == -1)
        return -errno;

    // full permission
    stbuf->st_mode |= 0777;

    return 0;
}

static int x_readdir(const char *path,
                     void *buf,
                     fuse_fill_dir_t filler,
                     off_t offset,
                     struct fuse_file_info *fi)
{
    (void) path;
    (void) offset;
    (void) fi;

    DIR *dp;
    struct dirent *de;

    dp = opendir(base_path);

    if (dp == NULL)
        return -errno;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    while ((de = readdir(dp)) != NULL)
    {
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0)
            continue;

        char name[1024];
        strcpy(name, de->d_name);

        char *ext = strstr(name, ".enc");

        if (ext != NULL)
            *ext = '\0';

        filler(buf, name, NULL, 0);
    }

    closedir(dp);

    return 0;
}

static int x_open(const char *path, struct fuse_file_info *fi)
{
    char fpath[1024];

    fullpath(fpath, path);

    int fd = open(fpath, fi->flags);

    if (fd == -1)
        return -errno;

    close(fd);

    return 0;
}

static int x_read(const char *path,
                  char *buf,
                  size_t size,
                  off_t offset,
                  struct fuse_file_info *fi)
{
    (void) fi;

    char fpath[1024];

    fullpath(fpath, path);

    int fd = open(fpath, O_RDONLY);

    if (fd == -1)
        return -errno;

    int res = pread(fd, buf, size, offset);

    if (res == -1)
    {
        close(fd);
        return -errno;
    }

    if (res > 0)
    {
        xor_crypt(buf, res);
    }

    close(fd);

    return res;
}

static int x_write(const char *path,
                   const char *buf,
                   size_t size,
                   off_t offset,
                   struct fuse_file_info *fi)
{
    (void) fi;

    char fpath[1024];

    fullpath(fpath, path);

    int fd = open(fpath, O_WRONLY);

    if (fd == -1)
        return -errno;

    char *enc = malloc(size);

    memcpy(enc, buf, size);

    xor_crypt(enc, size);

    int res = pwrite(fd, enc, size, offset);

    free(enc);

    if (res == -1)
    {
        close(fd);
        return -errno;
    }

    close(fd);

    return res;
}

static int x_create(const char *path,
                    mode_t mode,
                    struct fuse_file_info *fi)
{
    (void) fi;

    char fpath[1024];

    fullpath(fpath, path);

    int fd = creat(fpath, 0777);

    if (fd == -1)
        return -errno;

    close(fd);

    return 0;
}

static int x_truncate(const char *path, off_t size)
{
    char fpath[1024];

    fullpath(fpath, path);

    int res = truncate(fpath, size);

    if (res == -1)
        return -errno;

    return 0;
}

static int x_unlink(const char *path)
{
    char fpath[1024];

    fullpath(fpath, path);

    int res = unlink(fpath);

    if (res == -1)
        return -errno;

    return 0;
}

static struct fuse_operations x_oper = {
    .getattr = x_getattr,
    .readdir = x_readdir,
    .open = x_open,
    .read = x_read,
    .write = x_write,
    .create = x_create,
    .truncate = x_truncate,
    .unlink = x_unlink,
};

int main(int argc, char *argv[])
{
    umask(0);

    return fuse_main(argc, argv, &x_oper, NULL);
}
