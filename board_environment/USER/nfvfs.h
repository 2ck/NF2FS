// Copyright (C) 2022 Deadpool
//
// Nor Flash-based Virtual File System Implementation
//
// NORENV is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// NORENV is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with NORENV.  If not, see <http://www.gnu.org/licenses/>.

#ifndef __NFVFS_H
#define __NFVFS_H

#include <stdint.h>

#define NF_MAX_NAME_LEN 32
#define NF_MAX_OPEN_FILES 32

enum NFVFS_FILE_FLAGS
{
    O_RDONLY = 0x1,    // Open a file as read only
    O_WRONLY = 0x2,    // Open a file as write only
    O_RDWR = 0x3,      // Open a file as read and write
    O_CREAT = 0x0100,  // Create a file if it does not exist
    O_EXCL = 0x0200,   // Fail if a file already exists
    O_TRUNC = 0x0400,  // Truncate the existing file to zero size
    O_APPEND = 0x0800, // Move to end of file on every write
};

#define IF_O_RDONLY(flags) ((flags & O_RDONLY) == O_RDONLY)
#define IF_O_WRONLY(flags) ((flags & O_WRONLY) == O_WRONLY)
#define IF_O_RDWR(flags) ((flags & O_RDWR) == O_RDWR)
#define IF_O_CREAT(flags) ((flags & O_CREAT) == O_CREAT)
#define IF_O_EXCL(flags) ((flags & O_EXCL) == O_EXCL)
#define IF_O_TRUNC(flags) ((flags & O_TRUNC) == O_TRUNC)
#define IF_O_APPEND(flags) ((flags & O_APPEND) == O_APPEND)

enum NFVFS_FILE_MODE
{
    S_ISREG = 0x8000, // Regular file
    S_ISDIR = 0x4000, // Directory

    S_END = 0x4321,
};

#define S_IFREG(x) ((x)&S_ISREG)
#define S_IFDIR(x) ((x)&S_ISDIR)

enum NFVFS_SEEK_FLAG
{
    NFVFS_SEEK_SET = 0, // Seek from beginning of file
    NFVFS_SEEK_CUR = 1, // Seek from current position
    NFVFS_SEEK_END = 2, // Seek from end of file
};

enum NFVFS_FILE_TYPE
{
    NFVFS_TYPE_REG = 2,
    NFVFS_TYPE_DIR = 1,
    NFVFS_TYPE_END = 0,
};

enum NFVFS_REMOVE_TYPE
{
    NFVFS_REMOVE_FHANDLER = 0,
    NFVFS_REMOVE_PATH = 1,
};

struct nfvfs_fentry
{
    uint8_t fd;
    uint8_t used;
    int mode;
    void *f;
};

struct nfvfs_dentry
{
    uint8_t type;
    char *name;
};

struct nfvfs_context
{
    void *in_data;
    void *out_data;
};

struct nfvfs_operations {
  int (*mount)();
  int (*unmount)();
  int (*fssize)();
  int (*open)(char *path, int flags, int mode, struct nfvfs_context *context);
  int (*close)(int fd);
  int (*read)(int fd, void *buf, uint32_t size);
  int (*write)(int fd, void *buf, uint32_t size);
  int (*lseek)(int fd, uint32_t offset, int whence);
  int (*unlink)(const char *path);
  int (*rename)(const char *oldpath, const char *newpath);
  int (*mknod)(const char *path, int mode);
  int (*remove)(int fd, char *path, int mode);
  int (*opendir)(const char *path);
  int (*readdir)(int fd, struct nfvfs_dentry *buf);
  int (*closedir)(int fd);
  int (*stat)(const char *path, void *buf);
  int (*fstat)(int fd, void *buf);
  int (*truncate)(const char *path, int length);
  int (*ftruncate)(int fd, int length);
  int (*utime)(const char *path, void *times);
  int (*futime)(int fd, void *times);
  int (*link)(const char *oldpath, const char *newpath);
  int (*symlink)(const char *oldpath, const char *newpath);
  int (*readlink)(const char *path, char *buf, int bufsiz);
  int (*access)(const char *path, int amode);
  int (*faccess)(int fd, int amode);
  int (*fsync)(int fd);
  int (*fdatasync)(int fd);
  int (*sync)(int fd);
  int (*syncfs)(int fd);
  int (*ioctl)(int fd, int request, void *argp);
  int (*mmap)(void *addr, int len, int prot, int flags, int fd, int offset);
  int (*gc)(int size);
};

struct nfvfs {
  struct nfvfs *next;
  struct {
    struct nfvfs_operations op;
    void *my_private;
  } super;
  char name[NF_MAX_NAME_LEN];
  struct nfvfs_context context; /* used to pass parameters */
};

int register_nfvfs(const char *fsname, struct nfvfs_operations *op, void *data);
struct nfvfs *get_nfvfs(const char *fsname);
int unregister_nfvfs(const char *fsname);

int nfvfs_mount(struct nfvfs *);
int nfvfs_umount(struct nfvfs *);
int nfvfs_fssize(struct nfvfs *);

int nfvfs_open(struct nfvfs *, char *path, int flags, int mode);
int nfvfs_close(struct nfvfs *, int fd);
int nfvfs_read(struct nfvfs *, int fd, void *buf, int size);
int nfvfs_write(struct nfvfs *, int fd, void *buf, int size);
int nfvfs_lseek(struct nfvfs *, int fd, int offset, int whence);
int nfvfs_unlink(struct nfvfs *, const char *path);
int nfvfs_readdir(struct nfvfs *, int fd, struct nfvfs_dentry *buf);
int nfvfs_list(struct nfvfs *nfvfs, int fd,
               int (*action)(const char *name, void *data), void *data);

struct nfvfs_fentry *ftable_get_entry(int fd);
int nfvfs_remove(struct nfvfs *nfvfs, int fd, char *path, int mode);
int nfvfs_fsync(struct nfvfs *nfvfs, int fd);
int nfvfs_gc(struct nfvfs *nfvfs, int size);
int nfvfs_sync(struct nfvfs *nfvfs, int fd);

#endif /* __NFVFS_H */
