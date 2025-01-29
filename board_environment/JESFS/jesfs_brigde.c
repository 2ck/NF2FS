/**
 * Copyright (C) 2022 Deadpool, Hao Huang
 *
 * This file is part of NORENV.
 *
 * NORENV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * NORENV is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NORENV.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "FreeRTOS.h"
#include "jesfs_brigde.h"
#include "delay.h"
#include "jesfs.h"
#include "nfvfs.h"

int jesfs_mount_wrp()
{
    int err, tries;

    err = fs_start(FS_START_RESTART);
    if (err)
    {
        printf("jesfs starts failed: %d\r\n", err);
    }
    while (err)
    {
        printf("try formatting jesfs\r\n");
        err = fs_format(FS_FORMAT_FULL);
        if (err)
        {
            printf("format failed: %d\r\n", err);
            return err;
        }
        delay_ms(1000);
        tries++;
        if (tries >= 2)
        {
            return -1;
        }
    }
    return err;
}

int jesfs_unmount_wrp()
{
    return 0;
}

int jesfs_open_wrp(const char *path, int flags, int mode, struct nfvfs_context *context)
{
    FS_DESC *fs_desc;
    int fentry = *(int *)context->in_data;
    int jesfs_flags = 0;
    int ret = 0;

    jesfs_flags |= (IF_O_CREAT(flags) ? SF_OPEN_CREATE : 0);
    jesfs_flags |= (IF_O_RDONLY(flags) ? SF_OPEN_READ : 0);
    jesfs_flags |= (IF_O_WRONLY(flags) ? SF_OPEN_WRITE : 0);

    if (IF_O_RDWR(flags))
    {
        jesfs_flags |= SF_OPEN_READ;
        jesfs_flags |= SF_OPEN_WRITE;
    }

    // if (IF_O_CREAT(flags))
    // {
    //     jesfs_flags |= SF_OPEN_CREATE;
    //     jesfs_flags |= SF_OPEN_WRITE;
    // }
    // else if (IF_O_RDONLY(flags))
    // {
    //     jesfs_flags |= SF_OPEN_READ;
    // }

    if (IF_O_EXCL(flags) || IF_O_TRUNC(flags) || IF_O_APPEND(flags))
    {
        printf("%s: unsupported flags\r\n", __func__);
        return -1;
    }

    if (S_IFREG(mode))
    {
        fs_desc = (FS_DESC *)pvPortMalloc(sizeof(FS_DESC));
        ret = fs_open(fs_desc, (char *)path, jesfs_flags);
        context->out_data = fs_desc;
    }
    else
    {
        printf("%s: unsupported directory\r\n", __func__);
        return -1;
    }

    if (ret < 0)
    {
        return ret;
    }

    return fentry;
}

int jesfs_close_wrp(int fd)
{
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    if (entry == NULL)
    {
        return -1;
    }
    if (S_IFREG(entry->mode))
    {
        fs_close((FS_DESC *)entry->f);
    }
    else
    {
        printf("%s: unsupported directory\r\n", __func__);
        return -1;
    }
    vPortFree(entry->f);

    return 0;
}

int jesfs_read_wrp(int fd, void *buf, uint32_t size)
{
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    if (entry == NULL)
    {
        return -1;
    }
    if (S_IFREG(entry->mode))
    {
        return fs_read((FS_DESC *)entry->f, buf, size);
    }
    else
    {
        return -1;
    }
}

int jesfs_write_wrp(int fd, void *buf, uint32_t size)
{
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    if (entry == NULL)
    {
        return -1;
    }
    if (S_IFREG(entry->mode))
    {
        return fs_write((FS_DESC *)entry->f, buf, size);
    }
    else
    {
        return -1;
    }
}

int jesfs_lseek_wrp(int fd, uint32_t offset, int whence)
{
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    FS_DESC *fd_desc;
    if (entry == NULL)
    {
        return -1;
    }

    fd_desc = (FS_DESC *)entry->f;

    switch (whence)
    {
    case NFVFS_SEEK_CUR:
        if (offset != -fd_desc->file_pos)
        {
            printf("%s: unsupported offset\r\n", __func__);
            return -1;
        }
        break;
    case NFVFS_SEEK_SET:
        if (offset != 0)
        {
            printf("%s: unsupported offset\r\n", __func__);
            return -1;
        }
        break;
    case NFVFS_SEEK_END:
        if (offset != fd_desc->file_pos)
        {
            printf("%s: unsupported offset\r\n", __func__);
            return -1;
        }
        break;
    default:
        break;
    }

    if (S_IFREG(entry->mode))
    {
        return fs_rewind((FS_DESC *)entry->f);
    }
    else
    {
        return -1;
    }
}

int jesfs_delete_wrp(int fd, char *path, int mode)
{
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    FS_DESC *fd_desc;
    if (entry == NULL)
    {
        return -1;
    }

    fd_desc = (FS_DESC *)entry->f;
    return fs_delete(fd_desc);
}

int jesfs_fsync_wrp(int fd)
{
    return 0;
}

int jesfs_sync_wrp(int fd)
{
    return 0;
}

struct nfvfs_operations jesfs_ops = {
    .mount = jesfs_mount_wrp,
    .unmount = jesfs_unmount_wrp,
    .open = jesfs_open_wrp,
    .close = jesfs_close_wrp,
    .read = jesfs_read_wrp,
    .write = jesfs_write_wrp,
    .lseek = jesfs_lseek_wrp,
    .remove = jesfs_delete_wrp,
    .fsync = jesfs_fsync_wrp,
    .sync = jesfs_sync_wrp,
};
