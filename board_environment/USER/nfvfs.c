#include "nfvfs.h"
#include "FreeRTOS.h"
#include "string.h"

struct nfvfs_fentry ftable[NF_MAX_OPEN_FILES];
struct nfvfs nfvfs_guard;

static int translate_fd_fentry(int fd)
{
    int i;
    for (i = 0; i < NF_MAX_OPEN_FILES; i++) {
        if (ftable[i].fd == fd) {
            return i;
        }
    }

    return -1;
}

int register_nfvfs(const char *fsname, struct nfvfs_operations *op, void *data)
{
    struct nfvfs *nfvfs;

    nfvfs = pvPortMalloc(sizeof(struct nfvfs));
    if (!nfvfs)
        return -1;
    nfvfs->super.my_private = data;
    memcpy((void *)&nfvfs->super.op, (void *)op, sizeof(struct nfvfs_operations));
    strcpy((void *)nfvfs->name, (void *)fsname);
    nfvfs->next = nfvfs_guard.next;
    nfvfs_guard.next = nfvfs;

    return 0;
}

int unregister_nfvfs(const char *fsname)
{
    struct nfvfs *nfvfs, *prev;

    for (nfvfs = nfvfs_guard.next, prev = &nfvfs_guard; nfvfs; prev = nfvfs, nfvfs = nfvfs->next) {
        if (strcmp((void *)nfvfs->name, (void *)fsname) == 0) {
            prev->next = nfvfs->next;
            // NEXT
            vPortFree(nfvfs);
            // free(nfvfs);
            return 0;
        }
    }
    return -1;
}

struct nfvfs *get_nfvfs(const char *fsname)
{
    struct nfvfs *nfvfs;

    for (nfvfs = nfvfs_guard.next; nfvfs; nfvfs = nfvfs->next) {
        if (strcmp((void *)nfvfs->name, (void *)fsname) == 0) {
            return nfvfs;
        }
    }

    return NULL;
}

int nfvfs_mount(struct nfvfs *nfvfs)
{
    return nfvfs->super.op.mount();
}

int nfvfs_umount(struct nfvfs *nfvfs)
{
    return nfvfs->super.op.unmount();
}

int nfvfs_fssize(struct nfvfs *nfvfs)
{
    return nfvfs->super.op.fssize();
}

int nfvfs_open(struct nfvfs *nfvfs, char *path, int flags, int mode)
{
    int fd;
    int i;
    int fentry;

    for (i = 0; i < NF_MAX_OPEN_FILES; i++) {
        if (!ftable[i].used) {
            fentry = i;
            nfvfs->context.in_data = &fentry;

            fd = nfvfs->super.op.open(path, flags, mode, &nfvfs->context);
            if (fd < 0)
                return fd;
            ftable[i].used = 1;
            ftable[i].fd = fd;
            ftable[i].mode = mode;
            ftable[i].f = nfvfs->context.out_data;
            return i;
        }
    }

    return -1;
}

int nfvfs_close(struct nfvfs *nfvfs, int fd)
{
    int ret = 0;
    int fentry = translate_fd_fentry(fd);

    if (fentry < 0 || !ftable[fentry].used)
        return -1;

    ret = nfvfs->super.op.close(fentry);
    if (ret < 0)
        return ret;
    ftable[fentry].used = 0;

    return ret;
}

int nfvfs_read(struct nfvfs *nfvfs, int fd, void *buf, int size)
{
    int ret = 0;
    int fentry = translate_fd_fentry(fd);

    if (fentry < 0 || !ftable[fentry].used)
        return -1;

    ret = nfvfs->super.op.read(fentry, buf, size);
    if (ret < 0)
        return ret;

    return ret;
}

int nfvfs_write(struct nfvfs *nfvfs, int fd, void *buf, int size)
{
    int ret = 0;
    int fentry = translate_fd_fentry(fd);

    if (fentry < 0 || !ftable[fentry].used)
        return -1;

    ret = nfvfs->super.op.write(fentry, buf, size);
    if (ret < 0)
        return ret;

    return ret;
}

int nfvfs_lseek(struct nfvfs *nfvfs, int fd, int offset, int whence)
{
    int ret = 0;
    int fentry = translate_fd_fentry(fd);

    if (fentry < 0 || !ftable[fentry].used)
        return -1;

    ret = nfvfs->super.op.lseek(fentry, offset, whence);
    if (ret < 0)
        return ret;

    return ret;
}

int nfvfs_unlink(struct nfvfs *nfvfs, const char *path)
{
    return nfvfs->super.op.unlink(path);
}

int nfvfs_readdir(struct nfvfs *nfvfs, int fd, struct nfvfs_dentry *buf)
{
    int fentry = translate_fd_fentry(fd);
    return nfvfs->super.op.readdir(fentry, buf);
}

int nfvfs_list(struct nfvfs *nfvfs, int fd, int (*action)(const char *name, void *data), void *data)
{
    int fentry = translate_fd_fentry(fd);
    struct nfvfs_dentry dentry;
    int ret = 0;
    while (nfvfs_readdir(nfvfs, fentry, &dentry) == 0) {
        ret = action(dentry.name, data);
        if (ret != 0) {
            break;
        }
    }
    return ret;
}

struct nfvfs_fentry *ftable_get_entry(int fd)
{
    if (fd < 0 || fd >= NF_MAX_OPEN_FILES)
        return NULL;
    if (!ftable[fd].used)
        return NULL;
    return &ftable[fd];
}

int nfvfs_remove(struct nfvfs *nfvfs, int fd, char *path, int mode)
{
    if (*(nfvfs->name) == 'l' && *(nfvfs->name + 1) == 'i') {
        return nfvfs->super.op.remove(fd, path, mode);
    } else {
        int fentry = translate_fd_fentry(fd);
        if (fentry < 0 || !ftable[fentry].used)
            return -1;
        int ret = nfvfs->super.op.remove(fentry, path, mode);
        if (ret < 0)
            return ret;
        ftable[fentry].used = 0;
        return ret;
    }
}

int nfvfs_fsync(struct nfvfs *nfvfs, int fd)
{
    return nfvfs->super.op.fsync(fd);
}

int nfvfs_gc(struct nfvfs *nfvfs, int size)
{
    return nfvfs->super.op.gc(size);
}

int nfvfs_sync(struct nfvfs *nfvfs, int fd)
{
    return nfvfs->super.op.sync(fd);
}
