/**
 * Big file related related operations.
 */

#include "N2FS_file.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "N2FS.h"
#include "N2FS_head.h"
#include "N2FS_rw.h"
#include "N2FS_manage.h"
#include "N2FS_dir.h"
#include "N2FS_util.h"

// free a file in file list
int N2FS_file_free(N2FS_file_ram_t** list, N2FS_file_ram_t* file)
{
    N2FS_file_ram_t* head_file= *list;

    // If file is at the begin of list.
    if (head_file->id == file->id) {
        head_file = file->next_file;
        N2FS_free(file->file_cache.buffer);
        N2FS_free(file);
        *list= head_file;
        return N2FS_ERR_OK;
    }

    // Find file's previous file in file list.
    while (head_file->next_file != NULL) {
        if (head_file->next_file->id == file->id)
            break;
        head_file = head_file->next_file;
    }

    // free the file
    if (head_file->next_file == NULL) {
        return N2FS_ERR_NOFILEOPEN;
    } else {
        head_file->next_file = file->next_file;
        N2FS_ASSERT(head_file->next_file != file->next_file);
        N2FS_free(file->file_cache.buffer);
        N2FS_free(file);
        *list= head_file;
        return N2FS_ERR_OK;
    }
}

// prog function for big file data
int N2FS_bfile_prog(N2FS_t *N2FS, N2FS_size_t *sector, N2FS_off_t *off,
                      const void *buffer, N2FS_size_t len)
{
    int err = N2FS_ERR_OK;
    uint8_t *data = (uint8_t *)buffer;
    while (len > 0) {
        // Prog data to flash without head.
        N2FS_size_t size= N2FS_min(N2FS->cfg->sector_size - *off, len);
        err= N2FS_direct_prog(N2FS, N2FS_DIRECT_PROG_DATA, *sector,
                              *off, size, data);
        if (err)
            return err;

        // Change basic information.
        *off += size;
        len -= size;
        data += size;
        if (*off == N2FS->cfg->sector_size) {
            *sector += 1;
            *off = sizeof(N2FS_bfile_sector_flash_t);
        }
    }
    return err;
}

// find end sector of each index
void N2FS_end_sector_find(N2FS_t *N2FS, N2FS_bfile_index_ram_t *index,
                               N2FS_size_t num, N2FS_size_t *end_sector)
{
    for (int i = 0; i < num; i++) {
        end_sector[i] = index[i].sector;
        N2FS_off_t off = index[i].off;
        N2FS_size_t rest_size = index[i].size;

        while (rest_size > 0) {
            N2FS_size_t size = N2FS_min(N2FS->cfg->sector_size - off, rest_size);
            rest_size -= size;
            off += size;
            if (off == N2FS->cfg->sector_size) {
                end_sector[i]++;
                off = sizeof(N2FS_bfile_sector_flash_t);
            }
        }

        if (off == sizeof(N2FS_bfile_sector_flash_t))
            end_sector[i]--;
    }
}

// GC for parts of a very big file
int N2FS_bfile_part_gc(N2FS_t* N2FS, N2FS_file_ram_t* file, N2FS_size_t start, N2FS_size_t end,
                       N2FS_size_t len, N2FS_size_t index_num, N2FS_size_t sector_num)
{
    int err = N2FS_ERR_OK;

    // should not gc now
    if (sector_num > N2FS->manager->region_size)
        return err;

    // Find new sequential space to do gc.
    N2FS_size_t new_begin, new_sector;
    N2FS_off_t new_off = sizeof(N2FS_bfile_sector_flash_t);
    err = N2FS_sector_alloc(N2FS, N2FS->manager, N2FS_SECTOR_BFILE, sector_num,
                              N2FS_NULL, file->id, file->father_id, &new_sector, NULL);
    if (err)
        return err;
    new_begin= new_sector;

    // GC what need to gc.
    N2FS_bfile_index_flash_t *bfile_index = (N2FS_bfile_index_flash_t *)file->file_cache.buffer;
    for (int i = start; i <= end; i++) {
        N2FS_size_t sector = bfile_index->index[i].sector;
        N2FS_size_t off = bfile_index->index[i].off;
        N2FS_size_t rest_size = bfile_index->index[i].size;

        while (rest_size > 0) {
            // Read data of index sector.
            N2FS_size_t size= N2FS_min(N2FS->cfg->sector_size - off,
                                       N2FS_min(N2FS->cfg->cache_size, rest_size));
            err = N2FS_read_to_cache(N2FS, N2FS->rcache, sector, off, size);
            if (err)
                return err;

            // Prog readed data.
            err = N2FS_bfile_prog(N2FS, &new_sector, &new_off, N2FS->rcache->buffer, size);
            if (err)
                return err;

            // Update basic message.
            rest_size -= size;
            off+= size;
            if (off == N2FS->cfg->sector_size) {
                sector++;
                off = sizeof(N2FS_bfile_sector_flash_t);
            }
        }
    }

    // Turn sectors belongs to old index to old, so we can reuse them.
    err = N2FS_bfile_sector_old(N2FS, &bfile_index->index[start], end - start + 1);
    if (err)
        return err;

    // Turn old in-flash index to deleted.
    err = N2FS_data_delete(N2FS, file->father_id, file->file_cache.sector,
                             file->file_cache.off, file->file_cache.size);
    if (err)
        return err;

    // update the big file index
    bfile_index->index[start].sector = new_begin;
    bfile_index->index[start].off = sizeof(N2FS_bfile_sector_flash_t);
    bfile_index->index[start].size= len;
    N2FS_size_t rest = index_num - end - 1;
    memcpy(&bfile_index->index[start + 1], &bfile_index->index[end + 1], rest * sizeof(N2FS_bfile_index_ram_t));

    // update the file cache message
    file->file_cache.size-= (end - start) * sizeof(N2FS_bfile_index_ram_t);
    bfile_index->head= N2FS_MKDHEAD(0, 1, file->id, N2FS_DATA_BFILE_INDEX, file->file_cache.size);

    // find father dir
    N2FS_dir_ram_t* father_dir;
    err= N2FS_open_dir_find(N2FS, file->father_id, &father_dir);
    if (err)
        return err;

    // prog to flash
    err= N2FS_dir_prog(N2FS, father_dir, file->file_cache.buffer, file->file_cache.size);
    if (err)
        return err;

    // update basic message
    file->file_cache.sector= father_dir->tail_sector;
    file->file_cache.off= father_dir->tail_off - file->file_cache.size;
    file->file_cache.change_flag= false;
    return err;
}

// open file with file id.
int N2FS_file_lowopen(N2FS_t *N2FS, N2FS_dir_ram_t *dir, N2FS_size_t id,
                        N2FS_size_t sector, N2FS_off_t off, N2FS_size_t namelen,
                        N2FS_file_ram_t **file_addr)
{
    int err = N2FS_ERR_OK;

    // Find file in file list first.
    N2FS_file_ram_t* file= N2FS->file_list;
    N2FS_size_t cnt= 0;
    while (file != NULL) {
        if (file->id == id) {
            *file_addr = file;
            return err;
        }
        file= file->next_file;
        cnt++;
    }
    if (cnt >= N2FS_FILE_LIST_MAX)
        return N2FS_ERR_MUCHOPEN;

    // Allocate memory for file.
    file = N2FS_malloc(sizeof(N2FS_file_ram_t));
    if (!file) 
        return N2FS_ERR_NOMEM;

    // init basic message
    file->id = id;
    file->father_id= dir->id;
    file->file_pos= 0;

    file->sector = sector;
    file->off = off;
    file->namelen = namelen;

    // Allocate memory for buffer
    file->file_cache.buffer = N2FS_malloc(N2FS_FILE_CACHE_SIZE);
    if (!file->file_cache.buffer) {
        err= N2FS_ERR_NOMEM;
        goto cleanup;
    }
    memset(file->file_cache.buffer, 0xff, N2FS_FILE_CACHE_SIZE);

    // Traverse dir to find data with id.
    err = N2FS_dtraverse_data(N2FS, file);
    if (err)
        goto cleanup;
    N2FS_ASSERT(file->file_cache.sector != N2FS_NULL);

    // Add file to list.
    file->next_file = N2FS->file_list;
    N2FS->file_list = file;
    *file_addr = file;
    return err;

cleanup:
    if (file) {
        if (file->file_cache.buffer)
            N2FS_free(file->file_cache.buffer);
        N2FS_free(file);
    }
    N2FS_ERROR("err is in N2FS_file_lowopen\r\n");
    return err;
}

// Flush data in file cache to corresponding dir.
int N2FS_file_flush(N2FS_t *N2FS, N2FS_file_ram_t *file)
{
    int err = N2FS_ERR_OK;

    if (!file->file_cache.change_flag)
        return err;

    // Find file's father dir.
    N2FS_dir_ram_t* dir= N2FS->dir_list;
    while (dir != NULL) {
        if (dir->id == file->father_id)
            break;
        dir= dir->next_dir;
    }
    if (dir == NULL)
        return N2FS_ERR_NODIROPEN;

    // Set type of old file index to delete.
    N2FS_head_t old_head = *(N2FS_head_t *)file->file_cache.buffer;
    err = N2FS_data_delete(N2FS, file->father_id, file->file_cache.sector,
                             file->file_cache.off, N2FS_dhead_dsize(old_head));
    if (err)
        return err;

    // Prog new file index to dir.
    // in file cache, size and index are always new, but position and head may be old.
    N2FS_head_t *head = (N2FS_head_t *)file->file_cache.buffer;
    *head= N2FS_MKDHEAD(0, 1, file->id, (file->file_size <= N2FS_FILE_SIZE_THRESHOLD) ? N2FS_DATA_SFILE_DATA :
                        N2FS_DATA_BFILE_INDEX, file->file_cache.size);
    err = N2FS_dir_prog(N2FS, dir, file->file_cache.buffer, file->file_cache.size);
    if (err)
        return err;

    // update message
    file->file_cache.sector = dir->tail_sector;
    file->file_cache.off = dir->tail_off - file->file_cache.size;
    file->file_cache.change_flag= false;
    return err;
}

// create a new file
int N2FS_create_file(N2FS_t *N2FS, N2FS_dir_ram_t *dir, N2FS_file_ram_t **file_addr,
                       char *name, N2FS_size_t namelen)
{
    int err = N2FS_ERR_OK;

    // Allocate in-ram memory for file.
    N2FS_file_name_flash_t* flash_name= NULL;
    N2FS_file_ram_t* file= NULL;
    N2FS_size_t size= 0;
    file = N2FS_malloc(sizeof(N2FS_file_ram_t));
    if (!file)
        return N2FS_ERR_NOMEM;

    // Allocate in-ram memory for cache buffer of the file.
    file->file_cache.buffer = N2FS_malloc(N2FS_FILE_CACHE_SIZE);
    if (!file->file_cache.buffer) {
        err = N2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Allocate id for the new file.
    err = N2FS_id_alloc(N2FS, &file->id);
    if (err)
        goto cleanup;

    // Initialize basic message for the file.
    file->father_id = dir->id;
    file->file_size = 0;
    file->file_pos= 0;
    
    file->file_cache.sector = N2FS_NULL;
    file->file_cache.size= 0;
    file->file_cache.change_flag= false;

    // Create file name data and initialize it.
    size = sizeof(N2FS_file_name_flash_t) + namelen;
    flash_name = N2FS_malloc(size);
    if (flash_name == NULL) {
        err = N2FS_ERR_NOMEM;
        goto cleanup;
    }
    flash_name->head = N2FS_MKDHEAD(0, 1, file->id, N2FS_DATA_NFILE_NAME, size);
    memcpy(flash_name->name, name, namelen);

    // prog file name to its father dir
    err = N2FS_dir_prog(N2FS, dir, flash_name, size);
    N2FS_free(flash_name);
    flash_name = NULL;
    if (err)
        goto cleanup;

    // update file message
    file->sector = dir->tail_sector;
    file->off = dir->tail_off - size;
    file->namelen = namelen;
    *file_addr = file;

    file->next_file = N2FS->file_list;
    N2FS->file_list = file;
    return err;

cleanup:
    if (file) {
        if (file->file_cache.buffer)
            N2FS_free(file->file_cache.buffer);
        N2FS_free(file);
    }
    if (!flash_name)
        N2FS_free(flash_name);
    return err;
}

// read data of small file
int N2FS_small_file_read(N2FS_t *N2FS, N2FS_file_ram_t *file, void *buffer, N2FS_size_t size)
{
    N2FS_sfile_data_flash_t *sfile_data = (N2FS_sfile_data_flash_t *)file->file_cache.buffer;
    uint8_t *pos = sfile_data->data + file->file_pos;
    memcpy(buffer, pos, size);
    file->file_pos= file->file_pos + size;
    N2FS_ASSERT(file->file_pos <= file->file_size);
    return N2FS_ERR_OK;
}

// read big file data through (begin, off, len)
int N2FS_index_read_once(N2FS_t* N2FS, N2FS_size_t begin, N2FS_off_t off, N2FS_size_t len,
                         void *buffer)
{
    int err = N2FS_ERR_OK;

    // Change (begin, off) to valid (sector, off).
    N2FS_size_t sector = begin;
    while (off >= N2FS->cfg->sector_size) {
        sector++;
        off -= N2FS->cfg->sector_size;
    }

    // Read data to buffer directly
    uint8_t *data = (uint8_t *)buffer;
    while (len > 0) {
        // read data to buffer
        N2FS_size_t size= N2FS_min(N2FS->cfg->sector_size - off, len);
        err= N2FS_direct_read(N2FS, sector, off, size, data);
        if (err)
            return err;

        // update message
        data += size;
        len -= size;
        off += size;
        if (off == N2FS->cfg->sector_size) {
            off = sizeof(N2FS_bfile_sector_flash_t);
            sector++;
        }
    }
    return err;
}

// read data of big file
int N2FS_big_file_read(N2FS_t *N2FS, N2FS_file_ram_t *file, void *buffer, N2FS_size_t size)
{
    int err= N2FS_ERR_OK;
    N2FS_ASSERT(file->file_pos + size <= file->file_size);

    // Calculate the number of index the file has.
    int num = (file->file_cache.size - sizeof(N2FS_head_t)) /
              sizeof(N2FS_bfile_index_ram_t);
    N2FS_bfile_index_flash_t *index = (N2FS_bfile_index_flash_t *)file->file_cache.buffer;

    // Read module.
    N2FS_off_t off = 0;
    N2FS_size_t rest_size = size;
    uint8_t *data = (uint8_t *)buffer;
    for (int i = 0; i < num; i++) {
        if (off + index->index[i].size <= file->file_pos) {
            // skip what we do not need.
            off += index->index[i].size;
            continue;
        }

        // cal data to read in current index, and the read position
        N2FS_size_t len = N2FS_min(index->index[i].size - (file->file_pos - off), rest_size);
        N2FS_size_t temp_sector = index->index[i].sector;
        N2FS_off_t temp_off= index->index[i].off + (file->file_pos - off);

        // read data from the index
        err = N2FS_index_read_once(N2FS, temp_sector, temp_off, len, data);
        if (err)
            return err;

        data += len;
        file->file_pos += len;
        off += index->index[i].size;
        rest_size -= len;
        if (rest_size == 0)
            break;
    }
    N2FS_ASSERT(rest_size == 0);
    return err;
}

// write data to small file
int N2FS_small_file_write(N2FS_t *N2FS, N2FS_file_ram_t *file, const void *buffer, N2FS_size_t size)
{
    // Write to file buffer.
    N2FS_sfile_data_flash_t *small_file = (N2FS_sfile_data_flash_t *)file->file_cache.buffer;
    uint8_t *data = small_file->data + file->file_pos;
    memcpy(data, buffer, size);

    // Change message.
    file->file_pos += size;
    file->file_size = N2FS_max(file->file_size, file->file_pos);
    file->file_cache.size = file->file_size + sizeof(N2FS_head_t);
    file->file_cache.change_flag = true;
    return N2FS_ERR_OK;
}

// writa data and change file from small to big.
int N2FS_s2b_file_write(N2FS_t *N2FS, N2FS_file_ram_t *file, const void *buffer, N2FS_size_t size)
{
    int err = N2FS_ERR_OK;

    // alloc sectors we need
    N2FS_size_t sector = N2FS_NULL;
    N2FS_size_t off = sizeof(N2FS_bfile_sector_flash_t);
    N2FS_size_t num = N2FS_alignup(file->file_pos + size, N2FS->cfg->sector_size - off) /
                        (N2FS->cfg->sector_size - off);
    err = N2FS_sector_alloc(N2FS, N2FS->manager, N2FS_SECTOR_BFILE, num,
                              N2FS_NULL, file->id, file->father_id, &sector, NULL);
    if (err)
        return err;

    // Prog the valid data in file cache.
    N2FS_size_t begin = sector;
    if (file->file_pos > 0) {
        err = N2FS_bfile_prog(N2FS, &sector, &off, file->file_cache.buffer + sizeof(N2FS_head_t),
                                file->file_pos);
        if (err)
            return err;
    }

    // Prog the part of data in buffer.
    err = N2FS_bfile_prog(N2FS, &sector, &off, buffer, size);
    if (err)
        return err;

    // Change basic message.
    file->file_pos += size;
    file->file_size = file->file_pos;

    // Delete old data, if has not prog, then will not delete
    N2FS_size_t head = *(N2FS_head_t *)file->file_cache.buffer;
    err = N2FS_data_delete(N2FS, file->father_id, file->file_cache.sector,
                             file->file_cache.off, N2FS_dhead_dsize(head));
    if (err)
        return err;

    // Create new big file index data.
    N2FS_bfile_index_flash_t *bfile_index = (N2FS_bfile_index_flash_t *)file->file_cache.buffer;
    N2FS_size_t index_len = sizeof(N2FS_bfile_index_flash_t) + sizeof(N2FS_bfile_index_ram_t);
    bfile_index->head = N2FS_MKDHEAD(0, 1, file->id, N2FS_DATA_BFILE_INDEX, index_len);
    bfile_index->index[0].sector = begin;
    bfile_index->index[0].off = sizeof(N2FS_bfile_sector_flash_t);
    bfile_index->index[0].size = file->file_size;

    // Find file's father dir.
    N2FS_dir_ram_t* dir= NULL;
    err= N2FS_open_dir_find(N2FS, file->father_id, &dir);
    if (err)
        return err;

    // Change file cache message.
    file->file_cache.size = index_len;
    file->file_cache.change_flag = true;
    return N2FS_ERR_OK;
}

// set (begin, off, size) to (new_begin, new_off, size - jump_size)
void N2FS_index_jump(N2FS_t *N2FS, N2FS_bfile_index_ram_t *index, N2FS_size_t jump_size)
{
    N2FS_ASSERT(index->size >= jump_size);
    index->size -= jump_size;
    while (jump_size > 0) {
        N2FS_size_t size = N2FS_min(N2FS->cfg->sector_size - index->off, jump_size);
        jump_size -= size;
        index->off += size;
        if (index->off == N2FS->cfg->sector_size) {
            index->sector++;
            index->off = sizeof(N2FS_bfile_sector_flash_t);
        }
    }
}

// append write to big file.
int N2FS_big_file_append(N2FS_t* N2FS, N2FS_file_ram_t* file, void* buffer, N2FS_size_t size,
                         N2FS_bfile_index_ram_t *bfile_index, N2FS_size_t index_num)
{
    int err= N2FS_ERR_OK;

    // If it's appended write, We may use free space behind the last index.
    // Jump function is used to calculate free spcae in the last index.
    uint8_t *data = buffer;
    N2FS_bfile_index_ram_t temp_index = {
        .sector = bfile_index[index_num - 1].sector,
        .off = bfile_index[index_num - 1].off,
        .size = bfile_index[index_num - 1].size,
    };
    N2FS_index_jump(N2FS, &temp_index, temp_index.size);

    // If there is some free space, prog some data first.
    N2FS_size_t my_size= size;
    if (temp_index.off != sizeof(N2FS_bfile_sector_flash_t)) {
        // directly prog data to fill the free space
        N2FS_size_t len= N2FS_min(N2FS->cfg->sector_size - temp_index.off, my_size);
        err= N2FS_direct_prog(N2FS, N2FS_DIRECT_PROG_DATA, temp_index.sector,
                              temp_index.off, len, data);
        if (err)
            return err;

        // update message
        data += len;
        my_size -= len;
        bfile_index[index_num - 1].size += len;
        file->file_cache.change_flag = true;
        file->file_size += len;
        file->file_pos = file->file_size;

        if (my_size == 0)
            return err;
    }

    // alloc sector that we need
    N2FS_size_t sector = N2FS_NULL;
    N2FS_size_t off = sizeof(N2FS_bfile_sector_flash_t);
    N2FS_size_t num = N2FS_alignup(my_size, N2FS->cfg->sector_size - off) /
                        (N2FS->cfg->sector_size - off);
    err = N2FS_sector_alloc(N2FS, N2FS->manager, N2FS_SECTOR_BFILE, num,
                              N2FS_NULL, file->id, file->father_id, &sector, NULL);
    if (err)
        return err;

    // Prog data to flash.
    N2FS_size_t begin = sector;
    err = N2FS_bfile_prog(N2FS, &sector, &off, buffer, my_size);
    if (err)
        return err;

    N2FS_ASSERT((temp_index.sector != begin) || (temp_index.sector == begin && temp_index.off == sizeof(N2FS_bfile_sector_flash_t)));
    if (temp_index.sector + 1 == begin) {
        // If we can merge new index and the last old index.
        bfile_index[index_num - 1].size += my_size;
    } else {
        // add a new if they can not merge
        bfile_index[index_num].sector = begin;
        bfile_index[index_num].off = sizeof(N2FS_bfile_sector_flash_t);
        bfile_index[index_num].size = size;
        file->file_cache.size += sizeof(N2FS_bfile_index_ram_t);
    }

    // update basic message
    file->file_cache.change_flag = true;
    file->file_size += my_size;
    file->file_pos += my_size;
    return err;
}

// random write to big file
int N2FS_big_file_rwrite(N2FS_t* N2FS, N2FS_file_ram_t* file, void* buffer, N2FS_size_t size,
                         N2FS_bfile_index_ram_t *bfile_index, N2FS_size_t index_num){
    int err= N2FS_ERR_OK;

    // alloc sector that we need
    N2FS_size_t sector = N2FS_NULL;
    N2FS_size_t off = sizeof(N2FS_bfile_sector_flash_t);
    N2FS_ssize_t num = N2FS_alignup(size, N2FS->cfg->sector_size - off) /
                        (N2FS->cfg->sector_size - off);
    err = N2FS_sector_alloc(N2FS, N2FS->manager, N2FS_SECTOR_BFILE, num,
                              N2FS_NULL, file->id, file->father_id, &sector, NULL);
    if (err)
        return err;

    // Prog data to flash.
    N2FS_size_t begin = sector;
    err = N2FS_bfile_prog(N2FS, &sector, &off, buffer, size);
    if (err)
        return err;

    // make a new index
    N2FS_bfile_index_ram_t new_index = {
        .sector = begin,
        .off = sizeof(N2FS_bfile_sector_flash_t),
        .size = size,
    };

    // record if the first covered index during random write still has valid data
    N2FS_bfile_index_ram_t begin_index = {
        .sector = N2FS_NULL,
        .off = N2FS_NULL,
        .size = N2FS_NULL,
    };

    // record if the last covered index during random write still has valid data
    N2FS_bfile_index_ram_t end_index = {
        .sector = N2FS_NULL,
        .off = N2FS_NULL,
        .size = N2FS_NULL,
    };

    // Find the first index covered by new index.
    off = 0;
    int i = 0;
    for (i = 0; i < index_num; i++) {
        if (off + bfile_index[i].size <= file->file_pos)
            off += bfile_index[i].size;
        else
            break;
    }
    if (i == index_num)
        N2FS_ERROR("the index_num cal wrong!\r\n");

    // record valid data of the first covered index
    if (off != file->file_pos) {
        memcpy(&begin_index, &bfile_index[i], sizeof(N2FS_bfile_index_ram_t));
        begin_index.size = file->file_pos - off;
        N2FS_index_jump(N2FS, &bfile_index[i], file->file_pos - off);     
    }

    // If new data we prog can cover all data behind the begin_index
    if (file->file_pos + size >= file->file_size) {
        // if the first sector in index i still has valid data, we should not delete it.
        if (bfile_index[i].off > sizeof(N2FS_bfile_sector_flash_t)) {
            bfile_index[i].size -= N2FS_min(N2FS->cfg->sector_size - bfile_index[i].off,
                                            bfile_index[i].size);
            if (bfile_index[i].size == 0) {
                bfile_index[i].sector = N2FS_NULL;
            } else {
                bfile_index[i].off = sizeof(N2FS_bfile_index_flash_t);
                bfile_index[i].sector++;
            }
        }   

        // Set all deleted sectors to old.
        // TODO, some sector may still have valid data but deleted
        err = N2FS_bfile_sector_old(N2FS, &bfile_index[i], index_num - i);
        if (err)
            return err;

        if (begin_index.sector != N2FS_NULL) {
            memcpy(&bfile_index[i], &begin_index, sizeof(N2FS_bfile_index_ram_t));
            i++;
        }
        memcpy(&bfile_index[i], &new_index, sizeof(N2FS_bfile_index_ram_t));
        file->file_cache.size = (i + 1) * sizeof(N2FS_bfile_index_ram_t) + sizeof(N2FS_head_t);
        file->file_cache.change_flag = true;
        file->file_pos += size;
        file->file_size = file->file_pos;
        return err;
    }

    // Find the last index covered by new index.
    off = 0;
    int j = 0;
    for (j= i; j < index_num; j++) {
        // < maybe wrong, then change to <=
        if (off + bfile_index[j].size < size)
            off += bfile_index[j].size;
        else
            break;
    }

    // record valid of the last coverd index to end_index
    N2FS_ASSERT(off + bfile_index[j].size != size);
    memcpy(&end_index, &bfile_index[j], sizeof(N2FS_bfile_index_ram_t));
    N2FS_index_jump(N2FS, &end_index, size - off);
    bfile_index[j].size = (size - off);

    // if sectors belong to end index still has valid data, we should not delete them
    if (end_index.sector == bfile_index[j].sector) {
        bfile_index[j].sector = N2FS_NULL;
    } else {
        // should not delete the last sector if it still has valid data
        bfile_index[j].size -= (end_index.off - sizeof(N2FS_bfile_sector_flash_t));
    }

    if (i == j && (end_index.sector == bfile_index[i].sector)) {
        // If two of sectors are same, we can't free any sector.
        bfile_index[i].sector = N2FS_NULL;
    } else if (bfile_index[i].sector != N2FS_NULL && bfile_index[i].off > sizeof(N2FS_bfile_index_flash_t)) {
        // If the first sector still has valid data, we can not free it.
        bfile_index[i].size -= N2FS_min(N2FS->cfg->sector_size - bfile_index[i].off,
                                          bfile_index[i].size);
        if (bfile_index[i].size == 0) {
            bfile_index[i].sector = N2FS_NULL;
        } else {
            bfile_index[i].off = sizeof(N2FS_bfile_index_flash_t);
            bfile_index[i].sector++;
        }
    }

    // Set all deleted sectors to old.
    err = N2FS_bfile_sector_old(N2FS, &bfile_index[i], j - i + 1);
    if (err)
        return err;

    // Calculate number of new/changed index we should prog.
    N2FS_size_t new_index_num = 1;
    if (begin_index.sector != N2FS_NULL)
        new_index_num++;
    if (end_index.sector != N2FS_NULL)
        new_index_num++;

    if (j < index_num) {
        // the number of valid indexes behind index j
        num = index_num - j - 1;


        // TODO, change
        N2FS_ssize_t temp_num= num - 1;
        while (temp_num >= 0) {
            memcpy(&bfile_index[i + new_index_num + temp_num], &bfile_index[j + 1 + temp_num], sizeof(N2FS_bfile_index_ram_t));
            temp_num--;
        }
        // if (num > 0) {
        //     memcpy(&bfile_index[i + new_index_num], &bfile_index[j + 1],
        //            num * sizeof(N2FS_bfile_index_ram_t));
        // }
    }

    // Write begin index.
    int k = i;
    if (begin_index.sector != N2FS_NULL) {
        memcpy(&bfile_index[k], &begin_index, sizeof(N2FS_bfile_index_ram_t));
        k++;
    }

    // Write new index.
    memcpy(&bfile_index[k], &new_index, sizeof(N2FS_bfile_index_ram_t));
    k++;

    // Write end index.
    if (end_index.sector != N2FS_NULL) {
        memcpy(&bfile_index[k], &end_index, sizeof(N2FS_bfile_index_ram_t));
        k++;
    }

    // num is the number of index that deleted, new_index_num is new index that added
    num = j - i + 1;
    file->file_cache.size += (int)(new_index_num - num) * sizeof(N2FS_bfile_index_ram_t);
    file->file_cache.change_flag = true;
    file->file_pos = file->file_pos + size;
    file->file_size = N2FS_max(file->file_pos, file->file_size);
    N2FS_ASSERT(file->file_cache.size <= N2FS_FILE_CACHE_SIZE);
    return err;       
}

// write data to big file
int N2FS_big_file_write(N2FS_t *N2FS, N2FS_file_ram_t *file, void *buffer, N2FS_size_t size)
{
    // TODO in the future.
    // Now different data of different indexes don't use one sector

    int err = N2FS_ERR_OK;

    // Cal index number
    N2FS_bfile_index_flash_t *bfile = (N2FS_bfile_index_flash_t *)file->file_cache.buffer;
    N2FS_bfile_index_ram_t *bfile_index = bfile->index;
    N2FS_size_t index_num = (file->file_cache.size == 0) ? 0 : (file->file_cache.size - sizeof(N2FS_head_t)) / sizeof(N2FS_bfile_index_ram_t);

    // index number is too much, should gc and recal the index number
    if (index_num >= N2FS_FILE_INDEX_NUM) {
        err = N2FS_bfile_gc(N2FS, file);
        if (err)
            return err;
        index_num = (file->file_cache.size - sizeof(N2FS_head_t)) / sizeof(N2FS_bfile_index_ram_t);
    }

    // make sure there is enough space in cache for big file
    N2FS_ASSERT(file->file_cache.size + 2 * sizeof(N2FS_bfile_index_ram_t) <= N2FS_FILE_CACHE_SIZE);
    if (file->file_pos == file->file_size) {
        // append write the data
        err= N2FS_big_file_append(N2FS, file, buffer, size, bfile_index, index_num);
        if (err)
            return err;
    } else {
        // random write the data
        err= N2FS_big_file_rwrite(N2FS, file, buffer, size, bfile_index, index_num);
        if (err)
            return err;
    }
    return err;
}
