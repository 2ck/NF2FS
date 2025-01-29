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

#include "NF2FS_brigde.h"
#include "NF2FS.h"
#include "nfvfs.h"
// #include "w25qxx.h"
#include "nor_flash_simulate.h"

NF2FS_t NF2FS;

int W25Qxx_readNF2FS(const struct NF2FS_config *c, NF2FS_size_t sector,
                      NF2FS_off_t off, void *buffer, NF2FS_size_t size)
{
    if (sector >= W25Q256_NUM_GRAN) {
        return NF2FS_ERR_IO;
    }

    W25QXX_Read(buffer, sector * W25Q256_ERASE_GRAN + off, size);
    return NF2FS_ERR_OK;
}

int W25Qxx_writeNF2FS(const struct NF2FS_config *c, NF2FS_size_t sector,
                       NF2FS_off_t off, void *buffer, NF2FS_size_t size)
{
    if (sector >= W25Q256_NUM_GRAN) {
        return NF2FS_ERR_IO;
    }

    // W25QXX_Write(buffer, sector * W25Q256_ERASE_GRAN + off, size);
    W25QXX_Write_NoCheck(buffer, sector * W25Q256_ERASE_GRAN + off, size);

    return NF2FS_ERR_OK;
}

int W25Qxx_eraseNF2FS(const struct NF2FS_config *c, NF2FS_size_t sector)
{
    if (sector >= W25Q256_NUM_GRAN) {
        return NF2FS_ERR_IO;
    }

    W25QXX_Erase_Sector(sector);
    return NF2FS_ERR_OK;
}

int W25Qxx_syncNF2FS(const struct NF2FS_config *c)
{
    return NF2FS_ERR_OK;
}

const struct NF2FS_config NF2FS_cfg = {
    .read = W25Qxx_readNF2FS,
    .prog = W25Qxx_writeNF2FS,
    .erase = W25Qxx_eraseNF2FS,
    .sync = W25Qxx_syncNF2FS,

    .read_size = 1,
    .prog_size = 1,
    .sector_size = 4096,
    .sector_count = 8192,
    // .cache_size = 2048,
    // .cache_size = 512,
    .cache_size = 256,
    .region_cnt = 128,
    // .region_cnt = 64,
    .name_max = 255,
    .file_max = NF2FS_FILE_MAX_SIZE,
};

int NF2FS_mount_wrp()
{
    int err = -1;

    err = NF2FS_mount(&NF2FS, &NF2FS_cfg);
    if (err) {
        printf("mount fail is %d\r\n", err);
        return -1;
    }
    return NF2FS_ERR_OK;
}

int NF2FS_unmount_wrp()
{
    return NF2FS_unmount(&NF2FS);
}

int NF2FS_fssize_wrp()
{
    return 0;
}

int NF2FS_open_wrp(char *path, int flags, int mode, struct nfvfs_context *context)
{
    NF2FS_file_ram_t *file;
    NF2FS_dir_ram_t *dir;
    int fentry = *(int *)context->in_data;
    int err = NF2FS_ERR_OK;

    if (S_IFREG(mode)) {
        err = NF2FS_file_open(&NF2FS, &file, path, flags);
        context->out_data = file;
    } else {
        err = NF2FS_dir_open(&NF2FS, &dir, path);
        context->out_data = dir;
    }

    if (err < 0) {
        return err;
    }

    return fentry;
}

int NF2FS_close_wrp(int fd)
{
    int err = NF2FS_ERR_OK;
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    if (entry == NULL) {
        return -1;
    }

    if (S_IFREG(entry->mode)) {
        err = NF2FS_file_close(&NF2FS, (NF2FS_file_ram_t *)entry->f);
        if (err < 0) {
            printf("NF2FS_file_close error is %d\r\n", err);
        }
    } else {
        err = NF2FS_dir_close(&NF2FS, (NF2FS_dir_ram_t *)entry->f);
        if (err < 0) {
            printf("NF2FS_dir_close error is %d\r\n", err);
        }
    }
    entry->f = NULL;
    return NF2FS_ERR_OK;
}

int NF2FS_read_wrp(int fd, void *buf, uint32_t size)
{
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    if (entry == NULL) {
        return -1;
    }

    if (S_IFREG(entry->mode)) {
        return NF2FS_file_read(&NF2FS, (NF2FS_file_ram_t *)entry->f,
                                (void *)buf, size);
    } else {
        return -1;
    }
}

int NF2FS_write_wrp(int fd, void *buf, uint32_t size)
{
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    if (entry == NULL) {
        return -1;
    }

    if (S_IFREG(entry->mode)) {
        return NF2FS_file_write(&NF2FS, (NF2FS_file_ram_t *)entry->f,
                                 (void *)buf, size);
    } else {
        return -1;
    }
}

int NF2FS_lseek_wrp(int fd, uint32_t offset, int whence)
{
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    int NF2FS_whence;

    if (entry == NULL) {
        return -1;
    }

    switch (whence)
    {
    case NFVFS_SEEK_CUR:
        NF2FS_whence = NF2FS_SEEK_CUR;
        break;
    case NFVFS_SEEK_SET:
        NF2FS_whence = NF2FS_SEEK_SET;
        break;
    case NFVFS_SEEK_END:
        NF2FS_whence = NF2FS_SEEK_END;
        break;
    default:
        break;
    }

    if (S_IFREG(entry->mode)) {
        return NF2FS_file_seek(&NF2FS, (NF2FS_file_ram_t *)entry->f,
                                offset, NF2FS_whence);
    } else {
        return -1;
    }
}

int NF2FS_readdir_wrp(int fd, struct nfvfs_dentry *buf)
{
    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    NF2FS_info_ram_t my_info;

    int err = NF2FS_dir_read(&NF2FS, (NF2FS_dir_ram_t *)entry->f, &my_info);
    if (err < 0) {
        printf("read dir error type is %d\r\n", err);
        return -1;
    }

    if (my_info.type == 0)
        buf->type = (int)NFVFS_TYPE_END;
    else if (my_info.type == NF2FS_DATA_REG)
        buf->type = (int)NFVFS_TYPE_REG;
    else if (my_info.type == NF2FS_DATA_DIR)
        buf->type = (int)NFVFS_TYPE_DIR;
    else {
        printf("err in dir read function!\r\n");
        return -1;
    }

    strcpy(buf->name, my_info.name);
    return err;
}

int NF2FS_delete_wrp(int fd, char *path, int mode)
{
    int err = NF2FS_ERR_OK;

    struct nfvfs_fentry *entry = (struct nfvfs_fentry *)ftable_get_entry(fd);
    if (entry == NULL) {
        printf("can not get the file entry\r\n");
        return -1;
    }

    if (S_IFREG(mode)) {
        err = NF2FS_file_delete(&NF2FS, (NF2FS_file_ram_t *)entry->f);
        if (err != 0) {
            printf("Delete file error, err is %d\r\n", err);
        }
        return err;
    } else {
        err = NF2FS_dir_delete(&NF2FS, (NF2FS_dir_ram_t *)entry->f);
        if (err != 0) {
            printf("Delete dir err, err is %d\r\n", err);
        }
    }

    return err;
}

int NF2FS_fsync_wrp(int fd)
{
    int err = NF2FS_ERR_OK;

    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    if (entry == NULL) {
        return -1;
    }

    err = NF2FS_file_sync(&NF2FS, (NF2FS_file_ram_t *)entry->f);
    if (err < 0) {
        printf("file sync error is %d\r\n", err);
    }
    return err;
}

int NF2FS_sync_wrp(int fd)
{
    return 0;
}

struct nfvfs_operations NF2FS_ops = {
    .mount = NF2FS_mount_wrp,
    .unmount = NF2FS_unmount_wrp,
    .fssize = NF2FS_fssize_wrp,
    .open = NF2FS_open_wrp,
    .close = NF2FS_close_wrp,
    .read = NF2FS_read_wrp,
    .write = NF2FS_write_wrp,
    .lseek = NF2FS_lseek_wrp,
    .readdir = NF2FS_readdir_wrp,
    .remove = NF2FS_delete_wrp,
    .fsync = NF2FS_fsync_wrp,
    .sync = NF2FS_sync_wrp,
};
