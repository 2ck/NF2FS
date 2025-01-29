#include "spiffs_brigde.h"
#include "nfvfs.h"
#include "spiffs.h"
#include "w25qxx.h"
#include "delay.h"
#include "FreeRTOS.h"
#include "task.h"
#include "spiffs_nucleus.h"

#define LOG_PAGE_SIZE 256
// #define LOG_PAGE_SIZE 1024
#define W25Q256_SECTOR_SIZE 4096
#define W25Q256_SIZE (W25Q256_SECTOR_SIZE * W25Q256_NUM_GRAN)

spiffs spifs_fs;

static uint8_t spiffs_work_buf[LOG_PAGE_SIZE * 2];
static uint8_t spiffs_fds[32 * 4];
// TODO, change to (LOG_PAGE_SIZE + 32) * 10
static uint8_t spiffs_cache_buf[(LOG_PAGE_SIZE + 32) * 10];

int W25Qxx_readspiffs(u32_t addr, u32_t size, u8_t *dst)
{
    if (addr >= W25Q256_SIZE)
    {
        return SPIFFS_ERR_IO;
    }

    W25QXX_Read(dst, addr, size);

    return SPIFFS_OK;
}

int W25Qxx_writespiffs(u32_t addr, u32_t size, u8_t *src)
{
    if (addr >= W25Q256_SIZE)
    {
        return SPIFFS_ERR_IO;
    }

    // W25QXX_Write(src, addr, size);
    W25QXX_Write_NoCheck(src, addr, size);

    return SPIFFS_OK;
}

int W25Qxx_erasespiffs(u32_t addr, u32_t size)
{
    if (addr >= W25Q256_SIZE) // error
    {
        return SPIFFS_ERR_IO;
    }

    W25QXX_Erase_Sector(addr / 4096);
    return SPIFFS_OK;
}

int spiffs_mount_wrp()
{
    int err, tries = 0;
    spiffs_config cfg;
    cfg.phys_size = W25Q256_SIZE;              // use all spi flash
    cfg.phys_addr = 0;                         // start spiffs at start of spi flash
    cfg.phys_erase_block = W25Q256_ERASE_GRAN; // according to datasheet
    cfg.log_block_size = 65536;                // let us not complicate things
    cfg.log_page_size = LOG_PAGE_SIZE;         // as we said

    cfg.hal_read_f = W25Qxx_readspiffs;
    cfg.hal_write_f = W25Qxx_writespiffs;
    cfg.hal_erase_f = W25Qxx_erasespiffs;

    /* Do not config USE_MAGIC */
    err = SPIFFS_mount(&spifs_fs,
                       &cfg,
                       spiffs_work_buf,
                       spiffs_fds,
                       sizeof(spiffs_fds),
                       spiffs_cache_buf,
                       sizeof(spiffs_cache_buf),
                       NULL);
    while (err)
    {
        printf("try clean and remount %d\r\n", err);
        SPIFFS_format(&spifs_fs);
        err = SPIFFS_mount(&spifs_fs,
                           &cfg,
                           spiffs_work_buf,
                           spiffs_fds,
                           sizeof(spiffs_fds),
                           spiffs_cache_buf,
                           sizeof(spiffs_cache_buf),
                           NULL);
        // delay_ms(1000);
        tries++;
        if (tries >= 2)
        {
            printf("mount fail is %d\r\n", err);
            return -1;
        }
    }

    printf("mount res: %i\r\n", err);

    return err;
}

int spiffs_unmount_wrp()
{
    SPIFFS_unmount(&spifs_fs);
    return 0;
}

// int spiffs_fssize_wrp()
// {
//     // TODO
// }

int spiffs_open_wrp(char *path, int flags, int mode, struct nfvfs_context *context)
{
    int fentry = *(int *)context->in_data;
    int spiffs_flags = 0;
    int ret = 0;
    spiffs_file *fd;
    spiffs_DIR *dir;

    spiffs_flags |= (IF_O_CREAT(flags) ? SPIFFS_O_CREAT : 0);
    spiffs_flags |= (IF_O_EXCL(flags) ? SPIFFS_O_EXCL : 0);
    spiffs_flags |= (IF_O_TRUNC(flags) ? SPIFFS_O_TRUNC : 0);
    spiffs_flags |= (IF_O_APPEND(flags) ? SPIFFS_O_APPEND : 0);
    spiffs_flags |= (IF_O_RDONLY(flags) ? SPIFFS_O_RDONLY : 0);
    spiffs_flags |= (IF_O_WRONLY(flags) ? SPIFFS_O_WRONLY : 0);
    spiffs_flags |= (IF_O_RDWR(flags) ? SPIFFS_RDWR : 0);

    if (S_IFREG(mode))
    {
        fd = (spiffs_file *)pvPortMalloc(sizeof(spiffs_file));
        ret = SPIFFS_open(&spifs_fs, path, spiffs_flags, 0);
        if (ret < 0)
        {
            return ret;
        }
        *fd = ret;
        context->out_data = fd;
    }
    else
    {
        dir = (spiffs_DIR *)pvPortMalloc(sizeof(spiffs_DIR));
        SPIFFS_opendir(&spifs_fs, path, dir);
        context->out_data = dir;
    }

    return fentry;
}

int spiffs_close_wrp(int fd)
{
    int ret = 0;
    struct nfvfs_fentry *entry = (struct nfvfs_fentry *)ftable_get_entry(fd);
    if (entry == NULL)
    {
        return -1;
    }

    if (S_IFREG(entry->mode))
    {
        ret = SPIFFS_close(&spifs_fs, *(spiffs_file *)entry->f);
        vPortFree(entry->f);
    }
    else
    {
        SPIFFS_closedir((spiffs_DIR *)entry->f);
        vPortFree(entry->f);
    }

    return ret;
}

int spiffs_read_wrp(int fd, void *buf, uint32_t size)
{
    int ret = 0;
    struct nfvfs_fentry *entry = (struct nfvfs_fentry *)ftable_get_entry(fd);
    if (entry == NULL)
    {
        return -1;
    }

    if (S_IFREG(entry->mode))
    {
        ret = SPIFFS_read(&spifs_fs, *(spiffs_file *)entry->f, buf, size);
    }
    else
    {
        ret = -1;
    }

    return ret;
}

int spiffs_write_wrp(int fd, void *buf, uint32_t size)
{
    int ret = 0;
    struct nfvfs_fentry *entry = (struct nfvfs_fentry *)ftable_get_entry(fd);
    if (entry == NULL)
    {
        return -1;
    }

    if (S_IFREG(entry->mode))
    {
        ret = SPIFFS_write(&spifs_fs, *(spiffs_file *)entry->f, buf, size);
    }
    else
    {
        ret = -1;
    }

    return ret;
}

int spiffs_lseek_wrp(int fd, uint32_t offset, int whence)
{
    int ret = 0;
    struct nfvfs_fentry *entry = (struct nfvfs_fentry *)ftable_get_entry(fd);
    int spiffs_whence = 0;

    if (entry == NULL)
    {
        return -1;
    }

    switch (whence)
    {
    case NFVFS_SEEK_CUR:
        spiffs_whence = SPIFFS_SEEK_CUR;
        break;
    case NFVFS_SEEK_SET:
        spiffs_whence = SPIFFS_SEEK_SET;
        break;
    case NFVFS_SEEK_END:
        spiffs_whence = SPIFFS_SEEK_END;
        break;
    default:
        break;
    }

    if (S_IFREG(entry->mode))
    {
        ret = SPIFFS_lseek(&spifs_fs, *(spiffs_file *)entry->f, offset, spiffs_whence);
    }
    else
    {
        ret = -1;
    }

    return ret;
}

int spiffs_readdir_wrp(int fd, struct nfvfs_dentry *buf)
{
    int err = SPIFFS_OK;

    struct nfvfs_fentry *entry = (struct nfvfs_fentry *)ftable_get_entry(fd);
    struct spiffs_dirent my_info;

    struct spiffs_dirent *p = SPIFFS_readdir(entry->f, &my_info);
    if (p == NULL)
        buf->type = (int)NFVFS_TYPE_END;
    else if (my_info.type == SPIFFS_TYPE_FILE)
        buf->type = (int)NFVFS_TYPE_REG;
    else if (my_info.type == SPIFFS_TYPE_DIR)
        buf->type = (int)NFVFS_TYPE_DIR;
    else
    {
        printf("err in dir read function!\r\n");
        return -1;
    }

    strcpy(buf->name, (const char *)my_info.name);
    return err;
}

// Debug
int spiffs_fssize_wrp(void) {
    // return 0;
    SPIFFS_check(&spifs_fs);
}

int spiffs_delete_wrp(int fd, char *path, int mode)
{
    if (S_IFREG(mode))
    {
        // SPIFFS don't trully have a dir, so we make use of it to test the remove
        // function by filehandler.
        struct nfvfs_fentry *entry = (struct nfvfs_fentry *)ftable_get_entry(fd);
        if (entry == NULL)
            return -1;
        int err = SPIFFS_fremove(&spifs_fs, *(spiffs_file *)entry->f);
        vPortFree(entry->f);
        return err;
    }
    else
    {
        return SPIFFS_remove(&spifs_fs, path);
    }
}

int spiffs_fsync_wrp(int fd)
{
    int err = 0;

    struct nfvfs_fentry *entry = ftable_get_entry(fd);
    if (entry == NULL)
    {
        return -1;
    }

    err = SPIFFS_fflush(&spifs_fs, *(spiffs_file *)entry->f);
    if (err < 0)
    {
        printf("file sync error is %d\r\n", err);
    }
    return err;
}

int spiffs_gc_wrp()
{
    int err = 0;

    // u32_t size = (SPIFFS_PAGES_PER_BLOCK(&spifs_fs) - SPIFFS_OBJ_LOOKUP_PAGES(&spifs_fs)) *
    //                  (spifs_fs.block_count - 2) -
    //              spifs_fs.stats_p_allocated;
    // size = size * SPIFFS_DATA_PAGE_SIZE(&spifs_fs);

    u32_t size = (SPIFFS_PAGES_PER_BLOCK(&spifs_fs) - SPIFFS_OBJ_LOOKUP_PAGES(&spifs_fs)) * SPIFFS_DATA_PAGE_SIZE(&spifs_fs);

    // Debug
    // printf("page size is %d\r\n", SPIFFS_DATA_PAGE_SIZE(&spifs_fs));

    err = SPIFFS_gc(&spifs_fs, size);
    if (err < 0)
    {
        printf("file gc error is %d\r\n", err);
    }
    return err;
}

int spiffs_sync_wrp(int fd)
{
    spiffs_cache_init(&spifs_fs);
    return 0;
}

struct nfvfs_operations spiffs_ops = {
    .mount = spiffs_mount_wrp,
    .unmount = spiffs_unmount_wrp,
    .fssize = spiffs_fssize_wrp,
    .open = spiffs_open_wrp,
    .close = spiffs_close_wrp,
    .read = spiffs_read_wrp,
    .write = spiffs_write_wrp,
    .lseek = spiffs_lseek_wrp,
    .readdir = spiffs_readdir_wrp,
    .remove = spiffs_delete_wrp,
    .fsync = spiffs_fsync_wrp,
    .gc = spiffs_gc_wrp,
    .sync = spiffs_sync_wrp,
};
