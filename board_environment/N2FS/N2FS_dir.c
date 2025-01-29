/**
 * Dir related operations.
 */

#include "N2FS_dir.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "N2FS.h"
#include "N2FS_head.h"
#include "N2FS_rw.h"
#include "N2FS_tree.h"
#include "N2FS_manage.h"
#include "N2FS_util.h"

// NEXT
#include "FreeRTOS.h"

// Free specific dir in dir list.
int N2FS_dir_free(N2FS_dir_ram_t *list, N2FS_dir_ram_t *dir)
{
    // If dir is at the begin of list.
    if (list->id == dir->id) {
        list = dir->next_dir;
        N2FS_free(dir);
        return N2FS_ERR_OK;
    }

    // Find dir's previous dir in dir list.
    while (list->next_dir != NULL) {
        if (list->next_dir->id == dir->id)
            break;
        list = list->next_dir;
    }

    // free the dir
    if (list->next_dir == NULL)
        return N2FS_ERR_NODIROPEN;
    else {
        list->next_dir = dir->next_dir;
        N2FS_free(dir);
        return N2FS_ERR_OK;
    }
}

// Find the needed name address in the dir.
int N2FS_dtraverse_name(N2FS_t* N2FS, N2FS_size_t begin_sector, char* name,
                        N2FS_size_t namelen, int file_type, N2FS_tree_entry_ram_t* entry)
{
    int err= N2FS_ERR_OK;
    
    N2FS_size_t next_sector = N2FS_NULL;
    N2FS_size_t current_sector= begin_sector;
    N2FS_size_t dir_id= N2FS_NULL;
    N2FS_size_t off = 0;
    while (true) {
        // Read data of dir to cache first.
        N2FS_size_t size = N2FS_min(N2FS->cfg->cache_size,
                                    N2FS->cfg->sector_size - off);
        err = N2FS_read_to_cache(N2FS, N2FS->rcache, current_sector, off, size);
        if (err)
            return err;
        uint8_t *data = N2FS->rcache->buffer;

        // record the next sector of the dir
        if (off == 0) {
            N2FS_dir_sector_flash_t *shead = (N2FS_dir_sector_flash_t *)data;
            err = N2FS_shead_check(shead->head, N2FS_STATE_USING, N2FS_SECTOR_DIR);
            if (err)
                return err;
            next_sector= shead->pre_sector;
            dir_id= shead->id;
            data += sizeof(N2FS_dir_sector_flash_t);
            off += sizeof(N2FS_dir_sector_flash_t);
        }

        N2FS_head_t head;
        while (true) {
            head = *(N2FS_head_t *)data;

            // Check if the head is valid.
            err = N2FS_dhead_check(head, N2FS_NULL, (int)N2FS_NULL);
            if (err)
                return err;

            if (off + N2FS_dhead_dsize(head) > N2FS->rcache->off + size) {
                if (head == N2FS_NULL && next_sector != N2FS_NULL) {
                    // no more data in the sector, read the next
                    current_sector = next_sector;
                    off = 0;
                    break;
                } else if (head == N2FS_NULL) {
                    // not have next sector, finished and can not find
                    entry->id= N2FS_NULL;
                    return err;
                } else if (N2FS_dhead_type(head) != N2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            N2FS_size_t len;
            bool if_change = false;
            switch (N2FS_dhead_type(head)) {
            case N2FS_DATA_NDIR_NAME:
            case N2FS_DATA_DIR_NAME:
                len = N2FS_dhead_dsize(head);
                if (file_type == N2FS_DATA_DIR) {
                    // compare name and judge if matched
                    N2FS_dir_name_flash_t *fname = (N2FS_dir_name_flash_t *)data;
                    if (!memcmp(name, fname->name, namelen)) {
                        entry->id= N2FS_dhead_id(head);
                        entry->father_id= dir_id;
                        entry->name_sector= current_sector;
                        entry->name_off= off;
                        entry->tail_sector= fname->tail;
                        if (namelen <= N2FS_ENTRY_NAME_LEN) {
                            memcpy(entry->data.name, name, namelen);
                        } else {
                            // name is too long, use hash
                            entry->data.hash= N2FS_hash((uint8_t*)name, namelen);
                        }

                        // add dir to tree
                        err= N2FS_tree_entry_add(N2FS->ram_tree, entry->father_id, entry->id, entry->name_sector,
                                                 entry->name_off, entry->tail_sector, name, namelen);
                        return err;
                    }
                }
                break;

            case N2FS_DATA_NFILE_NAME:
            case N2FS_DATA_FILE_NAME:
                len = N2FS_dhead_dsize(head);
                if (file_type == N2FS_DATA_REG) {
                    N2FS_file_name_flash_t *fname = (N2FS_file_name_flash_t *)data;
                    if (!memcmp(name, fname->name, namelen)) {
                        // entry is not used for file, only stored necessary message
                        entry->id= N2FS_dhead_id(head);
                        entry->father_id= dir_id;
                        entry->name_sector= current_sector;
                        entry->name_off= off;
                        return err;
                    }
                }
                break;

            case N2FS_DATA_FREE:
                if (head == N2FS_NULL) {
                    // no more data, change to the next sector
                    if_change= true;
                } else {
                    // something has been wrong
                    N2FS_ERROR("WRONG in N2FS_dtraverse_name\n");
                    return N2FS_ERR_WRONGCAL;
                }
                len = 0;
                break;

            case N2FS_DATA_DELETE:
            case N2FS_DATA_BFILE_INDEX:
            case N2FS_DATA_SFILE_DATA:
            case N2FS_DATA_DIR_OSPACE:
                len = N2FS_dhead_dsize(head);
                break;

            default:
                N2FS_ERROR("WRONG in N2FS_dtraverse_name\n");
                return N2FS_ERR_WRONGCAL;
            }

            // update basic message
            off += len;
            data += len;

            if (if_change && next_sector != N2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                current_sector = next_sector;
                off = 0;
                break;
            } else if (if_change && next_sector == N2FS_NULL) {
                // fail to find the name
                entry->id= N2FS_NULL;
                return err;
            }

            // the next data head is not entire, read again
            if (off - N2FS->rcache->off + sizeof(N2FS_head_t) >= N2FS->cfg->cache_size)
                break;
        }
    }
}

// find file's data or index in the dir
int N2FS_dtraverse_data(N2FS_t* N2FS, N2FS_file_ram_t* file)
{
    int err= N2FS_ERR_OK;

    N2FS_size_t next_sector = N2FS_NULL;
    N2FS_size_t current_sector= file->sector;
    N2FS_size_t off= file->off;

    // if the cache is used for traversing name in the past, we can reuse it.
    if (N2FS->rcache->sector == file->sector &&
        N2FS->rcache->off <= file->off &&
        N2FS->rcache->off + N2FS->rcache->size > file->off)
        off= N2FS->rcache->off;

    while (true) {
        // Read data of file to cache first.
        N2FS_size_t size = N2FS_min(N2FS->cfg->cache_size,
                                    N2FS->cfg->sector_size - off);
        err = N2FS_read_to_cache(N2FS, N2FS->rcache, current_sector, off, size);
        if (err)
            return err;
        uint8_t *data = N2FS->rcache->buffer;

        // record the next sector of the file
        if (off == 0) {
            N2FS_dir_sector_flash_t *shead = (N2FS_dir_sector_flash_t *)data;
            err = N2FS_shead_check(shead->head, N2FS_STATE_USING, N2FS_SECTOR_DIR);
            if (err)
                return err;
            next_sector= shead->pre_sector;
            data += sizeof(N2FS_dir_sector_flash_t);
            off += sizeof(N2FS_dir_sector_flash_t);
        }

        N2FS_head_t head;
        while (true) {
            head = *(N2FS_head_t *)data;

            // Check if the head is valid.
            err = N2FS_dhead_check(head, N2FS_NULL, (int)N2FS_NULL);
            if (err) {
                N2FS_ERROR("head wrong in N2FS_dtraverse_data\r\n");
                return err;
            }

            if (off + N2FS_dhead_dsize(head) > N2FS->rcache->off + size) {
                if (head == N2FS_NULL && next_sector != N2FS_NULL) {
                    // no more data in the sector, read the next
                    current_sector = next_sector;
                    off = 0;
                    break;
                } else if (head == N2FS_NULL) {
                    // not have next sector, finished and can not find
                    file->file_cache.sector= N2FS_NULL;
                    return err;
                } else if (N2FS_dhead_type(head) != N2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            N2FS_size_t len;
            bool if_change = false;
            switch (N2FS_dhead_type(head)) {
            case N2FS_DATA_NDIR_NAME:
            case N2FS_DATA_DIR_NAME:
            case N2FS_DATA_NFILE_NAME:
            case N2FS_DATA_FILE_NAME:
            case N2FS_DATA_DELETE:
            case N2FS_DATA_DIR_OSPACE:
                len = N2FS_dhead_dsize(head);    
                break;

            case N2FS_DATA_FREE:
                if (head == N2FS_NULL) {
                    // no more data, change to the next sector
                    if_change= true;
                } else {
                    // something has been wrong
                    N2FS_ERROR("WRONG in N2FS_dtraverse_data\n");
                    return N2FS_ERR_WRONGCAL;
                }
                len = 0;
                break;

            case N2FS_DATA_BFILE_INDEX:
            case N2FS_DATA_SFILE_DATA:
                len = N2FS_dhead_dsize(head);
                if (N2FS_dhead_id(head) == file->id) {
                    if (off + len <= N2FS->rcache->off + size) {
                        // index is in cache
                        memcpy(file->file_cache.buffer, data, len);
                    } else {
                        // index is not entirely in cache, read directly
                        err= N2FS_direct_read(N2FS, current_sector, off, len, file->file_cache.buffer);
                        if (err) {
                            return err;
                        }
                    }
                    file->file_cache.sector= current_sector;
                    file->file_cache.off= off;
                    file->file_cache.change_flag= 0;
                    file->file_cache.size= len;
                    if (N2FS_dhead_type(head) == N2FS_DATA_SFILE_DATA) {
                        file->file_size= len - sizeof(N2FS_head_t);
                    } else {
                        file->file_size= 0;
                        N2FS_size_t loop= (len - sizeof(N2FS_head_t)) / sizeof(N2FS_bfile_index_ram_t);
                        N2FS_bfile_index_ram_t* index= (N2FS_bfile_index_ram_t*)(file->file_cache.buffer + sizeof(N2FS_head_t));
                        for (int i= 0; i < loop; i++)
                            file->file_size+= index[i].size;
                    }
                    return err;
                }
                break;

            default:
                N2FS_ERROR("WRONG in N2FS_dtraverse_data\n");
                return N2FS_ERR_WRONGCAL;
            }

            // update basic message
            off += len;
            data += len;

            if (if_change && next_sector != N2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                current_sector = next_sector;
                off = 0;
                break;
            } else if (if_change && next_sector == N2FS_NULL) {
                // fail to find the name
                file->file_cache.sector= N2FS_NULL;
                return err;
            }

            // the next data head is not entire, read again
            if (off - N2FS->rcache->off + sizeof(N2FS_head_t) >= N2FS->cfg->cache_size)
                break;
        }
    }
}

// delete all big files in current dir
int N2FS_dtraverse_bfile_delete(N2FS_t* N2FS, N2FS_dir_ram_t* dir)
{
    int err= N2FS_ERR_OK;

    N2FS_size_t next_sector = N2FS_NULL;
    N2FS_size_t current_sector= dir->tail_sector;
    N2FS_size_t off= 0;
    while (true) {
        // Read data of file to cache first.
        N2FS_size_t size = N2FS_min(N2FS->cfg->cache_size,
                                    N2FS->cfg->sector_size - off);
        err = N2FS_read_to_cache(N2FS, N2FS->rcache, current_sector, off, size);
        if (err)
            return err;
        uint8_t *data = N2FS->rcache->buffer;

        // record the next sector of the file
        if (off == 0) {
            N2FS_dir_sector_flash_t *shead = (N2FS_dir_sector_flash_t *)data;
            err = N2FS_shead_check(shead->head, N2FS_STATE_USING, N2FS_SECTOR_DIR);
            if (err)
                return err;
            next_sector= shead->pre_sector;
            data += sizeof(N2FS_dir_sector_flash_t);
            off += sizeof(N2FS_dir_sector_flash_t);
        }

        N2FS_head_t head;
        while (true) {
            head = *(N2FS_head_t *)data;

            // Check if the head is valid.
            err = N2FS_dhead_check(head, N2FS_NULL, (int)N2FS_NULL);
            if (err)
                return err;

            if (off + N2FS_dhead_dsize(head) > N2FS->rcache->off + size) {
                if (head == N2FS_NULL && next_sector != N2FS_NULL) {
                    // no more data in the sector, read the next
                    current_sector = next_sector;
                    off = 0;
                    break;
                } else if (head == N2FS_NULL) {
                    // not have next sector, finished and can not find
                    return err;
                } else if (N2FS_dhead_type(head) != N2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            N2FS_size_t len;
            bool if_change= false;
            switch (N2FS_dhead_type(head)) {
            case N2FS_DATA_NDIR_NAME:
            case N2FS_DATA_DIR_NAME:
            case N2FS_DATA_NFILE_NAME:
            case N2FS_DATA_FILE_NAME:
            case N2FS_DATA_DELETE:
            case N2FS_DATA_DIR_OSPACE:
            case N2FS_DATA_SFILE_DATA:
                len = N2FS_dhead_dsize(head);    
                break;

            case N2FS_DATA_FREE:
                if (head == N2FS_NULL) {
                    // no more data, change to the next sector
                    if_change= true;
                } else {
                    // something has been wrong
                    N2FS_ERROR("WRONG in N2FS_dtraverse_bfile_delete\n");
                    return N2FS_ERR_WRONGCAL;
                }
                len = 0;
                break;

            case N2FS_DATA_BFILE_INDEX: {
                // set sectors belong to big file data to old
                len = N2FS_dhead_dsize(head);
                N2FS_size_t index_num= (len - sizeof(N2FS_head_t)) / sizeof(N2FS_bfile_index_ram_t);
                N2FS_bfile_index_flash_t* bfile_index= (N2FS_bfile_index_flash_t*)data;

                if (off + len <= N2FS->rcache->off + size) {
                    // if index is entirely in cache
                    err = N2FS_bfile_sector_old(N2FS, bfile_index->index, index_num);
                    if (err)
                        return err;
                } else {
                    // If is not entirely in cache, we should use other approaches.
                    // in the next loop, rcache must reread, so we can change rcache now
                    N2FS_size_t index_sector= current_sector;
                    N2FS_size_t index_off= off + sizeof(N2FS_head_t);
                    while (index_num > 0) {
                        // read part index to rcache
                        N2FS_size_t read_size= N2FS_min(N2FS_aligndown(N2FS->cfg->cache_size, sizeof(N2FS_bfile_index_ram_t)),
                                                        index_num * sizeof(N2FS_bfile_index_ram_t));
                        err= N2FS_direct_read(N2FS, index_sector, index_off, read_size, N2FS->rcache->buffer);
                        if (err)
                            return err;

                        // set relative indexes to old
                        err= N2FS_bfile_sector_old(N2FS, (N2FS_bfile_index_ram_t*)N2FS->rcache->buffer,
                                                   read_size / sizeof(N2FS_bfile_index_ram_t));
                        if (err)
                            return err;

                        // update basic message
                        index_num-= read_size / sizeof(N2FS_bfile_index_ram_t);
                        index_off+= read_size;
                        N2FS_ASSERT(index_off <= N2FS->cfg->sector_size);
                    }
                }
                break;
            }

            default:
                N2FS_ERROR("WRONG in N2FS_dtraverse_bfile_delete\n");
                return N2FS_ERR_WRONGCAL;
            }

            // update basic message
            off += len;
            data += len;

            if (if_change && next_sector != N2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                current_sector = next_sector;
                off = 0;
                break;
            } else if (if_change && next_sector == N2FS_NULL) {
                // finishing
                return err;
            }

            // the next data head is not entire, read again
            if (off - N2FS->rcache->off + sizeof(N2FS_head_t) >= N2FS->cfg->cache_size)
                break;
        }
    }
}

// GC while traversing the dir
int N2FS_dtraverse_gc(N2FS_t* N2FS, N2FS_dir_ram_t* dir)
{
    int err= N2FS_ERR_OK;

    N2FS_size_t old_sector = dir->tail_sector;
    N2FS_off_t old_off= 0;
    N2FS_size_t next= N2FS_NULL;

    // New dir sector message.
    err= N2FS_sector_alloc(N2FS, N2FS->manager, N2FS_SECTOR_DIR, 1, N2FS_NULL,
                           dir->id, N2FS_NULL, &dir->tail_sector, NULL);
    if (err)
        return err;
    dir->tail_off= sizeof(N2FS_dir_sector_flash_t);

    // flush data in pcache
    err= N2FS_cache_flush(N2FS, N2FS->pcache);
    if (err)
        return err;
    N2FS_cache_one(N2FS, N2FS->pcache);

    dir->old_space= 0;
    while (true) {
        // Read data of old sector to cache first.
        N2FS_size_t size = N2FS_min(N2FS->cfg->cache_size, N2FS->cfg->sector_size - old_off);
        err = N2FS_read_to_cache(N2FS, N2FS->rcache, old_sector, old_off, size);
        if (err)
            return err;
        uint8_t *data = (uint8_t*)N2FS->rcache->buffer;

        // record the next sector to be traversed
        if (old_off == 0) {
            N2FS_dir_sector_flash_t *shead = (N2FS_dir_sector_flash_t *)data;
            err = N2FS_shead_check(shead->head, N2FS_STATE_USING, N2FS_SECTOR_DIR);
            if (err)
                return err;

            next = shead->pre_sector;
            data += sizeof(N2FS_dir_sector_flash_t);
            old_off += sizeof(N2FS_dir_sector_flash_t);
        }

        N2FS_head_t head;
        while (true) {
            head= *(N2FS_head_t*)data;

            // Check if the head is valid.
            err = N2FS_dhead_check(head, N2FS_NULL, (int)N2FS_NULL);
            if (err)
                return err;

            if (old_off + N2FS_dhead_dsize(head) > N2FS->rcache->off + size) {
                if (head == N2FS_NULL && next != N2FS_NULL) {
                    // no more data in the sector, read the next
                    old_sector = next;
                    old_off = 0;
                    break;
                } else if (head == N2FS_NULL) {
                    // not have next sector, finished and can not find
                    return err;
                } else if (N2FS_dhead_type(head) != N2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            N2FS_size_t len= 0;
            bool if_change= false;
            switch (N2FS_dhead_type(head))
            {
            case N2FS_DATA_DELETE:
            case N2FS_DATA_DIR_OSPACE:
                // Ignore delete data.
                len = N2FS_dhead_dsize(head);
                break;

            case N2FS_DATA_BFILE_INDEX:
                // Move to new sector.
                len= N2FS_dhead_dsize(head);
                if (old_off + len <= N2FS->pcache->off + size) {
                    // data is entirely in pcache, prog directly.
                    err = N2FS_dir_prog(N2FS, dir, data, len);
                    if (err)
                        return err;
                } else {
                    // If is not entirely in cache, we should use other approaches.
                    // in the next loop, pcache must reread, so we can change pcache now
                    N2FS_size_t temp_len= len;
                    N2FS_size_t temp_off= old_off;
                    N2FS_size_t data_type= N2FS_DIRECT_PROG_DHEAD;
                    while (temp_len > 0) {
                        // read part index to pcache
                        N2FS_size_t read_size= N2FS_min(N2FS->cfg->cache_size, temp_len);
                        err= N2FS_direct_read(N2FS, old_sector, temp_off, read_size, N2FS->pcache->buffer);
                        if (err)
                            return err;

                        // set relative indexes to old
                        err= N2FS_direct_prog(N2FS, data_type, old_sector, temp_off,
                                              read_size, N2FS->pcache->buffer);
                        if (err)
                            return err;
                        // means that other data do not need to change the written flag
                        data_type= N2FS_DIRECT_PROG_DATA; 

                        // update basic message
                        temp_len-= read_size;
                        temp_off+= read_size;
                        N2FS_ASSERT(temp_off <= N2FS->cfg->sector_size);
                    }
                }
                break;

            case N2FS_DATA_NDIR_NAME:
            case N2FS_DATA_NFILE_NAME:
            case N2FS_DATA_DIR_NAME:
            case N2FS_DATA_FILE_NAME:
            case N2FS_DATA_SFILE_DATA:
                // Move to new sector.
                len= N2FS_dhead_dsize(head);
                err = N2FS_dir_prog(N2FS, dir, data, len);
                if (err)
                    return err;

                // For son dir, we should update their tree entry message.
                if (N2FS_dhead_type(head) == N2FS_DATA_DIR_NAME ||
                    N2FS_dhead_type(head) == N2FS_DATA_NDIR_NAME) {
                    err= N2FS_tree_entry_update(N2FS->ram_tree, dir->id, dir->name_sector,
                                                dir->name_off, dir->tail_sector);
                    if (err)
                        return err;
                }
                break;

            case N2FS_DATA_FREE:
                if (head == N2FS_NULL) {
                    // no more data, change to the next sector
                    if_change= true;
                } else {
                    return N2FS_ERR_WRONGCAL;
                }

            default:
                N2FS_ERROR("WRONG in N2FS_dtraverse_bfile_delete\n");
                return N2FS_ERR_WRONGCAL;
            }

            // update basic message
            old_off += len;
            data += len;

            if (if_change && next != N2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                old_sector = next;
                old_off = 0;
                break;
            } else if (if_change && next == N2FS_NULL) {
                // finished
                return err;
            }

            // the next data head is not entire, read again
            if (old_off - N2FS->rcache->off + sizeof(N2FS_head_t) >= N2FS->cfg->cache_size) {
                break;
            }
        }
    }
}

// cal the old space in dir
int N2FS_dtraverse_ospace(N2FS_t *N2FS, N2FS_dir_ram_t *dir, N2FS_size_t sector, N2FS_cache_ram_t* cache)
{
    int err= N2FS_ERR_OK;

    // data structure need to use
    N2FS_size_t old_sector = sector;
    N2FS_off_t old_off= 0;
    N2FS_size_t next= N2FS_NULL;
    bool if_ospace= false;
    N2FS_size_t accu_ospace= 0;

    // init basic message
    dir->tail_sector= sector;
    dir->old_space= 0;
    while (true) {
        // Read data of old sector to cache first.
        N2FS_size_t size = N2FS_min(N2FS->cfg->cache_size, N2FS->cfg->sector_size - old_off);

        err = N2FS_read_to_cache(N2FS, cache, old_sector, old_off, size);
        if (err)
            return err;
        uint8_t *data = (uint8_t*)cache->buffer;

        // record the next sector to be traversed
        if (old_off == 0) {
            N2FS_dir_sector_flash_t *shead = (N2FS_dir_sector_flash_t *)data;
            err = N2FS_shead_check(shead->head, N2FS_STATE_USING, N2FS_SECTOR_DIR);
            if (err)
                return err;

            next = shead->pre_sector;
            data += sizeof(N2FS_dir_sector_flash_t);
            old_off += sizeof(N2FS_dir_sector_flash_t);
        }

        N2FS_head_t head;
        while (true) {
            head= *(N2FS_head_t*)data;

            // Check if the head is valid.
            err = N2FS_dhead_check(head, N2FS_NULL, (int)N2FS_NULL);
            if (err)
                return err;

            if (old_off + N2FS_dhead_dsize(head) > cache->off + size) {
                if (head == N2FS_NULL && next != N2FS_NULL) {
                    // no more data in the sector, read the next
                    old_sector = next;
                    old_off = 0;
                    break;
                } else if (head == N2FS_NULL) {
                    // not have next sector, finished and can not find
                    dir->tail_off= old_off;
                    return err;
                } else if (N2FS_dhead_type(head) != N2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            N2FS_size_t len= 0;
            bool if_change= false;
            switch (N2FS_dhead_type(head))
            {
            case N2FS_DATA_DELETE:
                // add to old space
                len= N2FS_dhead_dsize(head);
                dir->old_space+= len;
                break;

            case N2FS_DATA_DIR_OSPACE: {
                // get what we want, return if we find the tail_off.
                len= N2FS_dhead_dsize(head);
                if_ospace= true;
                N2FS_dir_ospace_flash_t *flash_old= (N2FS_dir_ospace_flash_t *)data;
                dir->old_space= flash_old->old_space + accu_ospace;
                dir->old_sector= old_sector;
                dir->old_off= old_off;
                break;
            }

            case N2FS_DATA_NDIR_NAME:
            case N2FS_DATA_NFILE_NAME:
            case N2FS_DATA_DIR_NAME:
            case N2FS_DATA_FILE_NAME:
            case N2FS_DATA_BFILE_INDEX:
            case N2FS_DATA_SFILE_DATA:
                // Move to new sector.
                len = N2FS_dhead_dsize(head);
                break;

            case N2FS_DATA_FREE:
                // get tail_off
                if (old_sector == dir->tail_sector) {
                    dir->tail_off= old_off;
                }
                
                if (head == N2FS_NULL) {
                    // no more data, ready change to the next sector to find ospace
                    if_change= true;
                    accu_ospace= accu_ospace + dir->old_space + N2FS->cfg->sector_size - old_off;
                    if (if_ospace) {
                        // alread find ospace, return
                        dir->old_space= accu_ospace;
                        return err;
                    }
                    dir->old_space= 0;
                } else {
                    return N2FS_ERR_WRONGCAL;
                }

            default:
                N2FS_ERROR("WRONG in N2FS_dtraverse_bfile_delete\n");
                return N2FS_ERR_WRONGCAL;
            }

            // update basic message
            old_off += len;
            data += len;

            if (if_change && next != N2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                old_sector = next;
                old_off = 0;
                break;
            } else if (if_change && next == N2FS_NULL) {
                // finished but not found.
                dir->old_sector= N2FS_NULL;
                dir->old_off= N2FS_NULL;
                dir->old_space= accu_ospace;
                return err;
            }

            // the next data head is not entire, read again
            if (old_off - cache->off + sizeof(N2FS_head_t) >= N2FS->cfg->cache_size)
                break;
        }
    }
}

// prog node to the dir
int N2FS_dir_prog(N2FS_t* N2FS, N2FS_dir_ram_t* dir, void* buffer, N2FS_size_t len)
{
    int err = N2FS_ERR_OK;
    N2FS_ASSERT(len < N2FS->cfg->sector_size);

    // get a new sector if there is no enough space
    if (dir->tail_off + len >= N2FS->cfg->sector_size) {
        // GC if there is enough space
        N2FS_ASSERT(dir->old_space != N2FS_NULL);
        if (dir->old_space >= N2FS->cfg->sector_size * 3) {
            // NEXT
            uint32_t start = (uint32_t)xTaskGetTickCount();

            err = N2FS_dir_gc(N2FS, dir);
            if (err)
                return err;

            // NEXT
            uint32_t end = (uint32_t)xTaskGetTickCount();
            printf("only gc time is %d\r\n", end - start);
        }

        // alloc a new sector if there still no enough space
        if (dir->tail_off + len >= N2FS->cfg->sector_size) {
            err= N2FS_sector_alloc(N2FS, N2FS->manager, N2FS_SECTOR_DIR, 1,
                                       dir->tail_sector, dir->id, N2FS_NULL, &dir->tail_sector, NULL);
            if (err)
                return err;
            dir->tail_off= sizeof(N2FS_dir_sector_flash_t);

            // update the in-flash tail message
            err= N2FS_dir_update(N2FS, dir);
            if (err)
                return err;
        }
    }

    // directly prog if data is larger than normal cache size
    if (len >= N2FS->cfg->cache_size) {
        // flush the pcache data
        err= N2FS_cache_flush(N2FS, N2FS->pcache);
        if (err)
            return err;

        // directly prog data to flash
        err= N2FS_direct_prog(N2FS, N2FS_DIRECT_PROG_DHEAD, dir->tail_sector, 
                              dir->tail_off, len, buffer);
        if (err)
            return err;
    } else {
        // prog data to flash
        err= N2FS_cache_prog(N2FS, N2FS->pcache, N2FS->rcache, dir->tail_sector,
                            dir->tail_off, buffer, len);
        if (err)
            return err;   
    }
    dir->tail_off+= len;
    N2FS_ASSERT(dir->tail_off <= N2FS->cfg->sector_size);
    return err;
}

// set bits in erase map to old.
int N2FS_dir_old(N2FS_t *N2FS, N2FS_size_t tail)
{
    int err = N2FS_ERR_OK;

    N2FS_dir_sector_flash_t dsector_head;
    while (tail != N2FS_NULL) {
        // read dir sector head
        err= N2FS_direct_read(N2FS, tail, 0, sizeof(N2FS_dir_sector_flash_t),
                              &dsector_head);
        if (err)
            return err;

        // Turn state of sector head to old.
        dsector_head.head&= N2FS_SHEAD_OLD_SET;
        err= N2FS_direct_prog(N2FS, N2FS_DIRECT_PROG_SHEAD, tail, 0,
                              sizeof(N2FS_head_t), &dsector_head.head);
        if (err)
            return err;

        // Turn erase map to reuse.
        err = N2FS_emap_set(N2FS, N2FS->manager, tail, 1);
        if (err)
            return err;
        tail = dsector_head.pre_sector;
    }
    return err;
}

// Update dir entry from its father dir.
int N2FS_dir_update(N2FS_t *N2FS, N2FS_dir_ram_t *dir)
{
    int err= N2FS_ERR_OK;

    N2FS_dir_name_flash_t *dir_name = NULL;
    N2FS_dir_ram_t *father_dir = NULL;
    N2FS_size_t len;

    // Find dir's corresponding entry.
    N2FS_size_t entry_index;
    err = N2FS_tree_entry_id_find(N2FS->ram_tree, dir->id, &entry_index);
    if (err)
        return err;

    // update tree entry
    N2FS->ram_tree->tree_array[entry_index].tail_sector= dir->tail_sector;

    // root dir do not need to update
    if (dir->father_id == N2FS_ID_SUPER && dir->id == N2FS_ID_ROOT) {
        // delete old root dir name in superblock
        err= N2FS_data_delete(N2FS, N2FS_ID_SUPER, dir->name_sector, dir->name_off, sizeof(N2FS_dir_name_flash_t));
        if (err)
            return err;

        // prog new root dir name in superblock
        N2FS_dir_name_flash_t temp_dir= {
            .head= N2FS_MKDHEAD(0, 1, N2FS_ID_ROOT, N2FS_DATA_DIR_NAME, sizeof(N2FS_dir_name_flash_t)),
            .tail= dir->tail_sector,
        };
        err= N2FS_prog_in_superblock(N2FS, N2FS->superblock, &temp_dir, sizeof(N2FS_dir_name_flash_t));
        if (err)
            return err;

        // update ram messages, namelen of root dir is 0
        dir->name_sector = N2FS->superblock->sector;
        dir->name_off= N2FS->superblock->free_off - len;
        dir->namelen= 0;
        N2FS->ram_tree->tree_array[entry_index].name_sector= dir->name_sector;
        N2FS->ram_tree->tree_array[entry_index].name_off= dir->name_off;
        return err;
    }

    // Find father dir in tree.
    N2FS_size_t father_index= N2FS_NULL;
    err= N2FS_tree_entry_id_find(N2FS->ram_tree, dir->father_id, &father_index);
    if (err)
        return err;

    // open father dir
    N2FS_tree_entry_ram_t* father_entry= &N2FS->ram_tree->tree_array[father_index];
    err= N2FS_dir_lowopen(N2FS, father_entry->tail_sector, father_entry->id, father_entry->father_id,
                          father_entry->name_sector, father_entry->name_off, &father_dir, N2FS->rcache);
    if (err)
        return err;

    // Read origin data to read cache.
    len = sizeof(N2FS_dir_name_flash_t) + dir->namelen;
    dir_name= N2FS_malloc(len);
    if (dir_name == NULL)
        return N2FS_ERR_NOMEM;

    // read origin name to flash and prog a new one
    err= N2FS_direct_read(N2FS, dir->name_sector, dir->name_off, len, dir_name);
    if (err)
        goto cleanup;
    dir_name->head= N2FS_MKDHEAD(0, 1, dir->id, N2FS_DATA_DIR_NAME, len);
    dir_name->tail= dir->tail_sector;
    N2FS_ASSERT(len == N2FS_dhead_dsize(dir_name->head));

    // Set origin data to delete.
    err = N2FS_data_delete(N2FS, dir->father_id, dir->name_sector, dir->name_off, len);
    if (err)
        goto cleanup;

    // Prog new data.
    err = N2FS_dir_prog(N2FS, father_dir, dir_name, len);
    if (err)
        goto cleanup;

    N2FS_cache_one(N2FS, N2FS->rcache);

    // Update address, the size of namelen is unchanged
    dir->name_sector = father_dir->tail_sector;
    dir->name_off= father_dir->tail_off - len;

    N2FS->ram_tree->tree_array[entry_index].name_sector= dir->name_sector;
    N2FS->ram_tree->tree_array[entry_index].name_off= dir->name_off;

cleanup:
    if (dir_name != NULL)
        N2FS_free(dir_name);
    return err;
}

// open dir
int N2FS_dir_lowopen(N2FS_t* N2FS, N2FS_size_t tail, N2FS_size_t id, N2FS_size_t father_id,
                     N2FS_size_t name_sector, N2FS_size_t name_off, N2FS_dir_ram_t **dir_addr,
                     N2FS_cache_ram_t* cache)
{
    int err = N2FS_ERR_OK;

    // If dir is opened, return
    N2FS_dir_ram_t *dir = N2FS->dir_list;
    while (dir != NULL) {
        if (dir->id == id) {
            *dir_addr = dir;
            return err;
        }
        dir = dir->next_dir;
    }

    // If not find, allocate memory for the dir.
    dir = N2FS_malloc(sizeof(N2FS_dir_ram_t));
    if (!dir)
        return N2FS_ERR_NOMEM;

    // Initialize basic data for the dir.
    dir->id = id;
    dir->father_id = father_id;
    dir->name_sector = name_sector;
    dir->name_off= name_off;

    // get namelen from flash
    N2FS_head_t head= N2FS_NULL;
    err= N2FS_direct_read(N2FS, name_sector, name_off, sizeof(N2FS_head_t), &head);
    if (err)
        return err;
    err= N2FS_dhead_check(head, dir->id, N2FS_NULL);
    if (err)
        return err;
    dir->namelen= N2FS_dhead_dsize(head) - sizeof(N2FS_dir_name_flash_t);
    
    dir->pos_sector = N2FS_NULL;
    dir->pos_off = N2FS_NULL;
    dir->pos_presector = N2FS_NULL;

    // cal old space in the dir
    err = N2FS_dtraverse_ospace(N2FS, dir, tail, cache);
    if (err)
        goto cleanup;

    // Add dir to dir list.
    dir->next_dir = N2FS->dir_list;
    N2FS->dir_list = dir;
    *dir_addr = dir;
    return err;

cleanup:
    N2FS_free(dir);
    return err;
}

// find opened dir with its id.
int N2FS_open_dir_find(N2FS_t* N2FS, N2FS_size_t id, N2FS_dir_ram_t** dir_addr)
{
    N2FS_dir_ram_t *fdir = N2FS->dir_list;
    while (fdir != NULL && fdir->id != id)
        fdir = fdir->next_dir;

    if (fdir == NULL)
        return N2FS_ERR_NODIROPEN;

    *dir_addr = fdir;
    return N2FS_ERR_OK;
}

// create a new dir
int N2FS_create_dir(N2FS_t* N2FS, N2FS_dir_ram_t* father_dir, N2FS_dir_ram_t** dir_addr, char* name, N2FS_size_t namelen)
{
    int err = N2FS_ERR_OK;

    N2FS_size_t size;

    // Create in-ram dir structure.
    N2FS_dir_name_flash_t *dir_name;
    N2FS_dir_ram_t *dir = *dir_addr;
    dir = N2FS_malloc(sizeof(N2FS_dir_ram_t));
    if (dir == NULL)
        return N2FS_ERR_NOMEM;

    // Allocate id for new dir.
    err = N2FS_id_alloc(N2FS, &dir->id);
    if (err)
        goto cleanup;

    // Allocate a sector for new dir.
    err = N2FS_sector_alloc(N2FS, N2FS->manager, N2FS_SECTOR_DIR, 1, N2FS_NULL,
                              dir->id, father_dir->id, &dir->tail_sector, NULL);
    if (err)
        goto cleanup;

    // Allocate memory for in-flash dir name structure.
    size = sizeof(N2FS_dir_name_flash_t) + namelen;
    dir_name = N2FS_malloc(size);
    if (!dir_name) {
        err = N2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Initialize data to dir name structure.
    dir_name->head = N2FS_MKDHEAD(0, 1, dir->id, N2FS_DATA_NDIR_NAME, size);
    dir_name->tail = dir->tail_sector;
    memcpy(dir_name->name, name, namelen);

    // Prog it to father dir.
    err = N2FS_dir_prog(N2FS, father_dir, dir_name, size);
    if (err)
        goto cleanup;

    // Update in-ram dir structure.
    dir->father_id= father_dir->id;
    
    dir->old_space= 0;
    dir->old_sector= N2FS_NULL;
    dir->old_off= N2FS_NULL;

    dir->pos_sector = N2FS_NULL;
    dir->pos_off = N2FS_NULL;
    dir->pos_presector= N2FS_NULL;

    dir->name_sector = father_dir->tail_sector;
    dir->name_off= father_dir->tail_off - size;
    dir->namelen= namelen;

    dir->tail_off= sizeof(N2FS_dir_sector_flash_t);

    // Add dir to dir list.
    dir->next_dir = N2FS->dir_list;
    N2FS->dir_list= dir;

    // Create new in-ram dir entry.
    err= N2FS_tree_entry_add(N2FS->ram_tree, father_dir->id, dir->id,
                             dir->name_sector, dir->name_off, dir->tail_sector, name, namelen);
    if (err)
        goto cleanup;

    // Return the new in-ram dir structure.
    *dir_addr = dir;
    N2FS_free(dir_name);
    return err;

cleanup:
    if (dir)
        N2FS_free(dir);
    if (dir_name)
        N2FS_free(dir_name);
    return err;
}
