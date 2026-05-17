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

static char source_dir[1024];

static int x_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    // FILE VIRTUAL
    if (strcmp(path, "/tujuan.txt") == 0)
    {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = 100;
        return 0;
    }

    char fullpath[1024];
    sprintf(fullpath, "%s%s", source_dir, path);

    if (lstat(fullpath, stbuf) == -1)
        return -errno;

    return 0;
}

static int x_readdir(const char *path,
                     void *buf,
                     fuse_fill_dir_t filler,
                     off_t offset,
                     struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    DIR *dp;
    struct dirent *de;

    char fullpath[1024];
    sprintf(fullpath, "%s%s", source_dir, path);

    dp = opendir(fullpath);

    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL)
    {
        filler(buf, de->d_name, NULL, 0);
    }

    // TAMBAH FILE VIRTUAL
    filler(buf, "tujuan.txt", NULL, 0);

    closedir(dp);

    return 0;
}

static int x_open(const char *path, struct fuse_file_info *fi)
{
    (void) fi;

    // FILE VIRTUAL
    if (strcmp(path, "/tujuan.txt") == 0)
        return 0;

    char fullpath[1024];
    sprintf(fullpath, "%s%s", source_dir, path);

    int fd = open(fullpath, O_RDONLY);

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

    // FILE VIRTUAL
    if (strcmp(path, "/tujuan.txt") == 0)
    {
        char final[256] = "Tujuan Mas Amba: ";

        char filepath[1024];
        char line[1024];
        char frag[128];

        for (int i = 1; i <= 7; i++)
        {
            sprintf(filepath, "%s/%d.txt", source_dir, i);

            FILE *fp = fopen(filepath, "r");

            if (fp == NULL)
                continue;

            while (fgets(line, sizeof(line), fp))
            {
                if (strncmp(line, "KOORD:", 6) == 0)
                {
                    sscanf(line, "KOORD: %[^\n]", frag);
                    strcat(final, frag);
                }
            }

            fclose(fp);
        }

        strcat(final, "\n");

        size_t len = strlen(final);

        if (offset < len)
        {
            if (offset + size > len)
                size = len - offset;

            memcpy(buf, final + offset, size);
        }
        else
        {
            size = 0;
        }

        return size;
    }

    // FILE ASLI
    char fullpath[1024];
    sprintf(fullpath, "%s%s", source_dir, path);

    int fd = open(fullpath, O_RDONLY);

    if (fd == -1)
        return -errno;

    int res = pread(fd, buf, size, offset);

    if (res == -1)
        res = -errno;

    close(fd);

    return res;
}

static struct fuse_operations x_oper = {
    .getattr = x_getattr,
    .readdir = x_readdir,
    .open = x_open,
    .read = x_read,
};

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <source_dir> <mountpoint>\n", argv[0]);
        return 1;
    }

    realpath(argv[1], source_dir);

    return fuse_main(argc - 1, argv + 1, &x_oper, NULL);
}
