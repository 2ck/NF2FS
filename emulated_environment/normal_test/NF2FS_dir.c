/**
 * Dir related operations.
 */

#include "NF2FS_dir.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "NF2FS.h"
#include "NF2FS_head.h"
#include "NF2FS_rw.h"
#include "NF2FS_tree.h"
#include "NF2FS_manage.h"
#include "NF2FS_util.h"

// // NEXT
// #include "FreeRTOS.h"

// Free specific dir in dir list.
int NF2FS_dir_free(NF2FS_dir_ram_t *list, NF2FS_dir_ram_t *dir)
{
    // If dir is at the begin of list.
    if (list->id == dir->id) {
        list = dir->next_dir;
        NF2FS_free(dir);
        return NF2FS_ERR_OK;
    }

    // Find dir's previous dir in dir list.
    while (list->next_dir != NULL) {
        if (list->next_dir->id == dir->id)
            break;
        list = list->next_dir;
    }

    // free the dir
    if (list->next_dir == NULL)
        return NF2FS_ERR_NODIROPEN;
    else {
        list->next_dir = dir->next_dir;
        NF2FS_free(dir);
        return NF2FS_ERR_OK;
    }
}

// Find the needed name address in the dir.
int NF2FS_dtraverse_name(NF2FS_t* NF2FS, NF2FS_size_t begin_sector, char* name,
                        NF2FS_size_t namelen, int file_type, NF2FS_tree_entry_ram_t* entry)
{
    int err= NF2FS_ERR_OK;
    
    NF2FS_size_t next_sector = NF2FS_NULL;
    NF2FS_size_t current_sector= begin_sector;
    NF2FS_size_t dir_id= NF2FS_NULL;
    NF2FS_size_t off = 0;
    while (true) {
        // Read data of dir to cache first.
        NF2FS_size_t size = NF2FS_min(NF2FS->cfg->cache_size,
                                    NF2FS->cfg->sector_size - off);
        err = NF2FS_read_to_cache(NF2FS, NF2FS->rcache, current_sector, off, size);
        if (err)
            return err;
        uint8_t *data = NF2FS->rcache->buffer;

        // record the next sector of the dir
        if (off == 0) {
            NF2FS_dir_sector_flash_t *shead = (NF2FS_dir_sector_flash_t *)data;
            err = NF2FS_shead_check(shead->head, NF2FS_STATE_USING, NF2FS_SECTOR_DIR);
            if (err)
                return err;
            next_sector= shead->pre_sector;
            dir_id= shead->id;
            data += sizeof(NF2FS_dir_sector_flash_t);
            off += sizeof(NF2FS_dir_sector_flash_t);
        }

        NF2FS_head_t head;
        while (true) {
            head = *(NF2FS_head_t *)data;

            // Check if the head is valid.
            err = NF2FS_dhead_check(head, NF2FS_NULL, (int)NF2FS_NULL);
            if (err)
                return err;

            if (off + NF2FS_dhead_dsize(head) > NF2FS->rcache->off + size) {
                if (head == NF2FS_NULL && next_sector != NF2FS_NULL) {
                    // no more data in the sector, read the next
                    current_sector = next_sector;
                    off = 0;
                    break;
                } else if (head == NF2FS_NULL) {
                    // not have next sector, finished and can not find
                    entry->id= NF2FS_NULL;
                    return err;
                } else if (NF2FS_dhead_type(head) != NF2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            NF2FS_size_t len;
            bool if_change = false;
            switch (NF2FS_dhead_type(head)) {
            case NF2FS_DATA_NDIR_NAME:
            case NF2FS_DATA_DIR_NAME:
                len = NF2FS_dhead_dsize(head);
                if (file_type == NF2FS_DATA_DIR) {
                    // compare name and judge if matched
                    NF2FS_dir_name_flash_t *fname = (NF2FS_dir_name_flash_t *)data;
                    if (!memcmp(name, fname->name, namelen)) {
                        entry->id= NF2FS_dhead_id(head);
                        entry->father_id= dir_id;
                        entry->name_sector= current_sector;
                        entry->name_off= off;
                        entry->tail_sector= fname->tail;
                        if (namelen <= NF2FS_ENTRY_NAME_LEN) {
                            memcpy(entry->data.name, name, namelen);
                        } else {
                            // name is too long, use hash
                            entry->data.hash= NF2FS_hash((uint8_t*)name, namelen);
                        }

                        // add dir to tree
                        err= NF2FS_tree_entry_add(NF2FS->ram_tree, entry->father_id, entry->id, entry->name_sector,
                                                 entry->name_off, entry->tail_sector, name, namelen);
                        return err;
                    }
                }
                break;

            case NF2FS_DATA_NFILE_NAME:
            case NF2FS_DATA_FILE_NAME:
                len = NF2FS_dhead_dsize(head);
                if (file_type == NF2FS_DATA_REG) {
                    NF2FS_file_name_flash_t *fname = (NF2FS_file_name_flash_t *)data;
                    if (!memcmp(name, fname->name, namelen)) {
                        // entry is not used for file, only stored necessary message
                        entry->id= NF2FS_dhead_id(head);
                        entry->father_id= dir_id;
                        entry->name_sector= current_sector;
                        entry->name_off= off;
                        return err;
                    }
                }
                break;

            case NF2FS_DATA_FREE:
                if (head == NF2FS_NULL) {
                    // no more data, change to the next sector
                    if_change= true;
                } else {
                    // something has been wrong
                    NF2FS_ERROR("WRONG in NF2FS_dtraverse_name\n");
                    return NF2FS_ERR_WRONGCAL;
                }
                len = 0;
                break;

            case NF2FS_DATA_DELETE:
            case NF2FS_DATA_BFILE_INDEX:
            case NF2FS_DATA_SFILE_DATA:
            case NF2FS_DATA_DIR_OSPACE:
                len = NF2FS_dhead_dsize(head);
                break;

            default:
                NF2FS_ERROR("WRONG in NF2FS_dtraverse_name\n");
                return NF2FS_ERR_WRONGCAL;
            }

            // update basic message
            off += len;
            data += len;

            if (if_change && next_sector != NF2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                current_sector = next_sector;
                off = 0;
                break;
            } else if (if_change && next_sector == NF2FS_NULL) {
                // fail to find the name
                entry->id= NF2FS_NULL;
                return err;
            }

            // the next data head is not entire, read again
            if (off - NF2FS->rcache->off + sizeof(NF2FS_head_t) >= NF2FS->cfg->cache_size)
                break;
        }
    }
}

// find file's data or index in the dir
int NF2FS_dtraverse_data(NF2FS_t* NF2FS, NF2FS_file_ram_t* file)
{
    int err= NF2FS_ERR_OK;

    NF2FS_size_t next_sector = NF2FS_NULL;
    NF2FS_size_t current_sector= file->sector;
    NF2FS_size_t off= file->off;

    // if the cache is used for traversing name in the past, we can reuse it.
    if (NF2FS->rcache->sector == file->sector &&
        NF2FS->rcache->off <= file->off &&
        NF2FS->rcache->off + NF2FS->rcache->size > file->off)
        off= NF2FS->rcache->off;

    while (true) {
        // Read data of file to cache first.
        NF2FS_size_t size = NF2FS_min(NF2FS->cfg->cache_size,
                                    NF2FS->cfg->sector_size - off);
        err = NF2FS_read_to_cache(NF2FS, NF2FS->rcache, current_sector, off, size);
        if (err)
            return err;
        uint8_t *data = NF2FS->rcache->buffer;

        // record the next sector of the file
        if (off == 0) {
            NF2FS_dir_sector_flash_t *shead = (NF2FS_dir_sector_flash_t *)data;
            err = NF2FS_shead_check(shead->head, NF2FS_STATE_USING, NF2FS_SECTOR_DIR);
            if (err)
                return err;
            next_sector= shead->pre_sector;
            data += sizeof(NF2FS_dir_sector_flash_t);
            off += sizeof(NF2FS_dir_sector_flash_t);
        }

        NF2FS_head_t head;
        while (true) {
            head = *(NF2FS_head_t *)data;

            // Check if the head is valid.
            err = NF2FS_dhead_check(head, NF2FS_NULL, (int)NF2FS_NULL);
            if (err) {
                NF2FS_ERROR("head wrong in NF2FS_dtraverse_data\r\n");
                return err;
            }

            if (off + NF2FS_dhead_dsize(head) > NF2FS->rcache->off + size) {
                if (head == NF2FS_NULL && next_sector != NF2FS_NULL) {
                    // no more data in the sector, read the next
                    current_sector = next_sector;
                    off = 0;
                    break;
                } else if (head == NF2FS_NULL) {
                    // not have next sector, finished and can not find
                    file->file_cache.sector= NF2FS_NULL;
                    return err;
                } else if (NF2FS_dhead_type(head) != NF2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            NF2FS_size_t len;
            bool if_change = false;
            switch (NF2FS_dhead_type(head)) {
            case NF2FS_DATA_NDIR_NAME:
            case NF2FS_DATA_DIR_NAME:
            case NF2FS_DATA_NFILE_NAME:
            case NF2FS_DATA_FILE_NAME:
            case NF2FS_DATA_DELETE:
            case NF2FS_DATA_DIR_OSPACE:
                len = NF2FS_dhead_dsize(head);    
                break;

            case NF2FS_DATA_FREE:
                if (head == NF2FS_NULL) {
                    // no more data, change to the next sector
                    if_change= true;
                } else {
                    // something has been wrong
                    NF2FS_ERROR("WRONG in NF2FS_dtraverse_data\n");
                    return NF2FS_ERR_WRONGCAL;
                }
                len = 0;
                break;

            case NF2FS_DATA_BFILE_INDEX:
            case NF2FS_DATA_SFILE_DATA:
                len = NF2FS_dhead_dsize(head);
                if (NF2FS_dhead_id(head) == file->id) {
                    if (off + len <= NF2FS->rcache->off + size) {
                        // index is in cache
                        memcpy(file->file_cache.buffer, data, len);
                    } else {
                        // index is not entirely in cache, read directly
                        err= NF2FS_direct_read(NF2FS, current_sector, off, len, file->file_cache.buffer);
                        if (err) {
                            return err;
                        }
                    }
                    file->file_cache.sector= current_sector;
                    file->file_cache.off= off;
                    file->file_cache.change_flag= 0;
                    file->file_cache.size= len;
                    if (NF2FS_dhead_type(head) == NF2FS_DATA_SFILE_DATA) {
                        file->file_size= len - sizeof(NF2FS_head_t);
                    } else {
                        file->file_size= 0;
                        NF2FS_size_t loop= (len - sizeof(NF2FS_head_t)) / sizeof(NF2FS_bfile_index_ram_t);
                        NF2FS_bfile_index_ram_t* index= (NF2FS_bfile_index_ram_t*)(file->file_cache.buffer + sizeof(NF2FS_head_t));
                        for (int i= 0; i < loop; i++)
                            file->file_size+= index[i].size;
                    }
                    return err;
                }
                break;

            default:
                NF2FS_ERROR("WRONG in NF2FS_dtraverse_data\n");
                return NF2FS_ERR_WRONGCAL;
            }

            // update basic message
            off += len;
            data += len;

            if (if_change && next_sector != NF2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                current_sector = next_sector;
                off = 0;
                break;
            } else if (if_change && next_sector == NF2FS_NULL) {
                // fail to find the name
                file->file_cache.sector= NF2FS_NULL;
                return err;
            }

            // the next data head is not entire, read again
            if (off - NF2FS->rcache->off + sizeof(NF2FS_head_t) >= NF2FS->cfg->cache_size)
                break;
        }
    }
}

// delete all big files in current dir
int NF2FS_dtraverse_bfile_delete(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir)
{
    int err= NF2FS_ERR_OK;

    NF2FS_size_t next_sector = NF2FS_NULL;
    NF2FS_size_t current_sector= dir->tail_sector;
    NF2FS_size_t off= 0;
    while (true) {
        // Read data of file to cache first.
        NF2FS_size_t size = NF2FS_min(NF2FS->cfg->cache_size,
                                    NF2FS->cfg->sector_size - off);
        err = NF2FS_read_to_cache(NF2FS, NF2FS->rcache, current_sector, off, size);
        if (err)
            return err;
        uint8_t *data = NF2FS->rcache->buffer;

        // record the next sector of the file
        if (off == 0) {
            NF2FS_dir_sector_flash_t *shead = (NF2FS_dir_sector_flash_t *)data;
            err = NF2FS_shead_check(shead->head, NF2FS_STATE_USING, NF2FS_SECTOR_DIR);
            if (err)
                return err;
            next_sector= shead->pre_sector;
            data += sizeof(NF2FS_dir_sector_flash_t);
            off += sizeof(NF2FS_dir_sector_flash_t);
        }

        NF2FS_head_t head;
        while (true) {
            head = *(NF2FS_head_t *)data;

            // Check if the head is valid.
            err = NF2FS_dhead_check(head, NF2FS_NULL, (int)NF2FS_NULL);
            if (err)
                return err;

            if (off + NF2FS_dhead_dsize(head) > NF2FS->rcache->off + size) {
                if (head == NF2FS_NULL && next_sector != NF2FS_NULL) {
                    // no more data in the sector, read the next
                    current_sector = next_sector;
                    off = 0;
                    break;
                } else if (head == NF2FS_NULL) {
                    // not have next sector, finished and can not find
                    return err;
                } else if (NF2FS_dhead_type(head) != NF2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            NF2FS_size_t len;
            bool if_change= false;
            switch (NF2FS_dhead_type(head)) {
            case NF2FS_DATA_NDIR_NAME:
            case NF2FS_DATA_DIR_NAME:
            case NF2FS_DATA_NFILE_NAME:
            case NF2FS_DATA_FILE_NAME:
            case NF2FS_DATA_DELETE:
            case NF2FS_DATA_DIR_OSPACE:
            case NF2FS_DATA_SFILE_DATA:
                len = NF2FS_dhead_dsize(head);    
                break;

            case NF2FS_DATA_FREE:
                if (head == NF2FS_NULL) {
                    // no more data, change to the next sector
                    if_change= true;
                } else {
                    // something has been wrong
                    NF2FS_ERROR("WRONG in NF2FS_dtraverse_bfile_delete\n");
                    return NF2FS_ERR_WRONGCAL;
                }
                len = 0;
                break;

            case NF2FS_DATA_BFILE_INDEX: {
                // set sectors belong to big file data to old
                len = NF2FS_dhead_dsize(head);
                NF2FS_size_t index_num= (len - sizeof(NF2FS_head_t)) / sizeof(NF2FS_bfile_index_ram_t);
                NF2FS_bfile_index_flash_t* bfile_index= (NF2FS_bfile_index_flash_t*)data;

                if (off + len <= NF2FS->rcache->off + size) {
                    // if index is entirely in cache
                    err = NF2FS_bfile_sector_old(NF2FS, bfile_index->index, index_num);
                    if (err)
                        return err;
                } else {
                    // If is not entirely in cache, we should use other approaches.
                    // in the next loop, rcache must reread, so we can change rcache now
                    NF2FS_size_t index_sector= current_sector;
                    NF2FS_size_t index_off= off + sizeof(NF2FS_head_t);
                    while (index_num > 0) {
                        // read part index to rcache
                        NF2FS_size_t read_size= NF2FS_min(NF2FS_aligndown(NF2FS->cfg->cache_size, sizeof(NF2FS_bfile_index_ram_t)),
                                                        index_num * sizeof(NF2FS_bfile_index_ram_t));
                        err= NF2FS_direct_read(NF2FS, index_sector, index_off, read_size, NF2FS->rcache->buffer);
                        if (err)
                            return err;

                        // set relative indexes to old
                        err= NF2FS_bfile_sector_old(NF2FS, (NF2FS_bfile_index_ram_t*)NF2FS->rcache->buffer,
                                                   read_size / sizeof(NF2FS_bfile_index_ram_t));
                        if (err)
                            return err;

                        // update basic message
                        index_num-= read_size / sizeof(NF2FS_bfile_index_ram_t);
                        index_off+= read_size;
                        NF2FS_ASSERT(index_off <= NF2FS->cfg->sector_size);
                    }
                }
                break;
            }

            default:
                NF2FS_ERROR("WRONG in NF2FS_dtraverse_bfile_delete\n");
                return NF2FS_ERR_WRONGCAL;
            }

            // update basic message
            off += len;
            data += len;

            if (if_change && next_sector != NF2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                current_sector = next_sector;
                off = 0;
                break;
            } else if (if_change && next_sector == NF2FS_NULL) {
                // finishing
                return err;
            }

            // the next data head is not entire, read again
            if (off - NF2FS->rcache->off + sizeof(NF2FS_head_t) >= NF2FS->cfg->cache_size)
                break;
        }
    }
}

// GC while traversing the dir
int NF2FS_dtraverse_gc(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir)
{
    int err= NF2FS_ERR_OK;

    NF2FS_size_t old_sector = dir->tail_sector;
    NF2FS_off_t old_off= 0;
    NF2FS_size_t next= NF2FS_NULL;

    // New dir sector message.
    err= NF2FS_sector_alloc(NF2FS, NF2FS->manager, NF2FS_SECTOR_DIR, 1, NF2FS_NULL,
                           dir->id, NF2FS_NULL, &dir->tail_sector, NULL);
    if (err)
        return err;
    dir->tail_off= sizeof(NF2FS_dir_sector_flash_t);

    // flush data in pcache
    err= NF2FS_cache_flush(NF2FS, NF2FS->pcache);
    if (err)
        return err;
    NF2FS_cache_one(NF2FS, NF2FS->pcache);

    dir->old_space= 0;
    while (true) {
        // Read data of old sector to cache first.
        NF2FS_size_t size = NF2FS_min(NF2FS->cfg->cache_size, NF2FS->cfg->sector_size - old_off);
        err = NF2FS_read_to_cache(NF2FS, NF2FS->rcache, old_sector, old_off, size);
        if (err)
            return err;
        uint8_t *data = (uint8_t*)NF2FS->rcache->buffer;

        // record the next sector to be traversed
        if (old_off == 0) {
            NF2FS_dir_sector_flash_t *shead = (NF2FS_dir_sector_flash_t *)data;
            err = NF2FS_shead_check(shead->head, NF2FS_STATE_USING, NF2FS_SECTOR_DIR);
            if (err)
                return err;

            next = shead->pre_sector;
            data += sizeof(NF2FS_dir_sector_flash_t);
            old_off += sizeof(NF2FS_dir_sector_flash_t);
        }

        NF2FS_head_t head;
        while (true) {
            head= *(NF2FS_head_t*)data;

            // Check if the head is valid.
            err = NF2FS_dhead_check(head, NF2FS_NULL, (int)NF2FS_NULL);
            if (err)
                return err;

            if (old_off + NF2FS_dhead_dsize(head) > NF2FS->rcache->off + size) {
                if (head == NF2FS_NULL && next != NF2FS_NULL) {
                    // no more data in the sector, read the next
                    old_sector = next;
                    old_off = 0;
                    break;
                } else if (head == NF2FS_NULL) {
                    // not have next sector, finished and can not find
                    return err;
                } else if (NF2FS_dhead_type(head) != NF2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            NF2FS_size_t len= 0;
            bool if_change= false;
            switch (NF2FS_dhead_type(head))
            {
            case NF2FS_DATA_DELETE:
            case NF2FS_DATA_DIR_OSPACE:
                // Ignore delete data.
                len = NF2FS_dhead_dsize(head);
                break;

            case NF2FS_DATA_BFILE_INDEX:
                // Move to new sector.
                len= NF2FS_dhead_dsize(head);
                if (old_off + len <= NF2FS->pcache->off + size) {
                    // data is entirely in pcache, prog directly.
                    err = NF2FS_dir_prog(NF2FS, dir, data, len);
                    if (err)
                        return err;
                } else {
                    // If is not entirely in cache, we should use other approaches.
                    // in the next loop, pcache must reread, so we can change pcache now
                    NF2FS_size_t temp_len= len;
                    NF2FS_size_t temp_off= old_off;
                    NF2FS_size_t data_type= NF2FS_DIRECT_PROG_DHEAD;
                    while (temp_len > 0) {
                        // read part index to pcache
                        NF2FS_size_t read_size= NF2FS_min(NF2FS->cfg->cache_size, temp_len);
                        err= NF2FS_direct_read(NF2FS, old_sector, temp_off, read_size, NF2FS->pcache->buffer);
                        if (err)
                            return err;

                        // set relative indexes to old
                        err= NF2FS_direct_prog(NF2FS, data_type, old_sector, temp_off,
                                              read_size, NF2FS->pcache->buffer);
                        if (err)
                            return err;
                        // means that other data do not need to change the written flag
                        data_type= NF2FS_DIRECT_PROG_DATA; 

                        // update basic message
                        temp_len-= read_size;
                        temp_off+= read_size;
                        NF2FS_ASSERT(temp_off <= NF2FS->cfg->sector_size);
                    }
                }
                break;

            case NF2FS_DATA_NDIR_NAME:
            case NF2FS_DATA_NFILE_NAME:
            case NF2FS_DATA_DIR_NAME:
            case NF2FS_DATA_FILE_NAME:
            case NF2FS_DATA_SFILE_DATA:
                // Move to new sector.
                len= NF2FS_dhead_dsize(head);
                err = NF2FS_dir_prog(NF2FS, dir, data, len);
                if (err)
                    return err;

                // For son dir, we should update their tree entry message.
                if (NF2FS_dhead_type(head) == NF2FS_DATA_DIR_NAME ||
                    NF2FS_dhead_type(head) == NF2FS_DATA_NDIR_NAME) {
                    err= NF2FS_tree_entry_update(NF2FS->ram_tree, dir->id, dir->name_sector,
                                                dir->name_off, dir->tail_sector);
                    if (err)
                        return err;
                }
                break;

            case NF2FS_DATA_FREE:
                if (head == NF2FS_NULL) {
                    // no more data, change to the next sector
                    if_change= true;
                } else {
                    return NF2FS_ERR_WRONGCAL;
                }

            default:
                NF2FS_ERROR("WRONG in NF2FS_dtraverse_bfile_delete\n");
                return NF2FS_ERR_WRONGCAL;
            }

            // update basic message
            old_off += len;
            data += len;

            if (if_change && next != NF2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                old_sector = next;
                old_off = 0;
                break;
            } else if (if_change && next == NF2FS_NULL) {
                // finished
                return err;
            }

            // the next data head is not entire, read again
            if (old_off - NF2FS->rcache->off + sizeof(NF2FS_head_t) >= NF2FS->cfg->cache_size) {
                break;
            }
        }
    }
}

// cal the old space in dir
int NF2FS_dtraverse_ospace(NF2FS_t *NF2FS, NF2FS_dir_ram_t *dir, NF2FS_size_t sector, NF2FS_cache_ram_t* cache)
{
    int err= NF2FS_ERR_OK;

    // data structure need to use
    NF2FS_size_t old_sector = sector;
    NF2FS_off_t old_off= 0;
    NF2FS_size_t next= NF2FS_NULL;
    bool if_ospace= false;
    NF2FS_size_t accu_ospace= 0;

    // init basic message
    dir->tail_sector= sector;
    dir->old_space= 0;
    while (true) {
        // Read data of old sector to cache first.
        NF2FS_size_t size = NF2FS_min(NF2FS->cfg->cache_size, NF2FS->cfg->sector_size - old_off);

        err = NF2FS_read_to_cache(NF2FS, cache, old_sector, old_off, size);
        if (err)
            return err;
        uint8_t *data = (uint8_t*)cache->buffer;

        // record the next sector to be traversed
        if (old_off == 0) {
            NF2FS_dir_sector_flash_t *shead = (NF2FS_dir_sector_flash_t *)data;
            err = NF2FS_shead_check(shead->head, NF2FS_STATE_USING, NF2FS_SECTOR_DIR);
            if (err)
                return err;

            next = shead->pre_sector;
            data += sizeof(NF2FS_dir_sector_flash_t);
            old_off += sizeof(NF2FS_dir_sector_flash_t);
        }

        NF2FS_head_t head;
        while (true) {
            head= *(NF2FS_head_t*)data;

            // Check if the head is valid.
            err = NF2FS_dhead_check(head, NF2FS_NULL, (int)NF2FS_NULL);
            if (err)
                return err;

            if (old_off + NF2FS_dhead_dsize(head) > cache->off + size) {
                if (head == NF2FS_NULL && next != NF2FS_NULL) {
                    // no more data in the sector, read the next
                    old_sector = next;
                    old_off = 0;
                    break;
                } else if (head == NF2FS_NULL) {
                    // not have next sector, finished and can not find
                    dir->tail_off= old_off;
                    return err;
                } else if (NF2FS_dhead_type(head) != NF2FS_DATA_BFILE_INDEX) {
                    // We have read whole data in cache.
                    // but big file data is special
                    break;
                }
            }

            NF2FS_size_t len= 0;
            bool if_change= false;
            switch (NF2FS_dhead_type(head))
            {
            case NF2FS_DATA_DELETE:
                // add to old space
                len= NF2FS_dhead_dsize(head);
                dir->old_space+= len;
                break;

            case NF2FS_DATA_DIR_OSPACE: {
                // get what we want, return if we find the tail_off.
                len= NF2FS_dhead_dsize(head);
                if_ospace= true;
                NF2FS_dir_ospace_flash_t *flash_old= (NF2FS_dir_ospace_flash_t *)data;
                dir->old_space= flash_old->old_space + accu_ospace;
                dir->old_sector= old_sector;
                dir->old_off= old_off;
                break;
            }

            case NF2FS_DATA_NDIR_NAME:
            case NF2FS_DATA_NFILE_NAME:
            case NF2FS_DATA_DIR_NAME:
            case NF2FS_DATA_FILE_NAME:
            case NF2FS_DATA_BFILE_INDEX:
            case NF2FS_DATA_SFILE_DATA:
                // Move to new sector.
                len = NF2FS_dhead_dsize(head);
                break;

            case NF2FS_DATA_FREE:
                // get tail_off
                if (old_sector == dir->tail_sector) {
                    dir->tail_off= old_off;
                }
                
                if (head == NF2FS_NULL) {
                    // no more data, ready change to the next sector to find ospace
                    if_change= true;
                    accu_ospace= accu_ospace + dir->old_space + NF2FS->cfg->sector_size - old_off;
                    if (if_ospace) {
                        // alread find ospace, return
                        dir->old_space= accu_ospace;
                        return err;
                    }
                    dir->old_space= 0;
                } else {
                    return NF2FS_ERR_WRONGCAL;
                }

            default:
                NF2FS_ERROR("WRONG in NF2FS_dtraverse_bfile_delete\n");
                return NF2FS_ERR_WRONGCAL;
            }

            // update basic message
            old_off += len;
            data += len;

            if (if_change && next != NF2FS_NULL) {
                // Traverse the next sector.
                if_change = false;
                old_sector = next;
                old_off = 0;
                break;
            } else if (if_change && next == NF2FS_NULL) {
                // finished but not found.
                dir->old_sector= NF2FS_NULL;
                dir->old_off= NF2FS_NULL;
                dir->old_space= accu_ospace;
                return err;
            }

            // the next data head is not entire, read again
            if (old_off - cache->off + sizeof(NF2FS_head_t) >= NF2FS->cfg->cache_size)
                break;
        }
    }
}

// prog node to the dir
int NF2FS_dir_prog(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir, void* buffer, NF2FS_size_t len)
{
    int err = NF2FS_ERR_OK;
    NF2FS_ASSERT(len < NF2FS->cfg->sector_size);

    // get a new sector if there is no enough space
    if (dir->tail_off + len >= NF2FS->cfg->sector_size) {
        // GC if there is enough space
        NF2FS_ASSERT(dir->old_space != NF2FS_NULL);
        if (dir->old_space >= NF2FS->cfg->sector_size * 3) {
            // // NEXT
            // uint32_t start = (uint32_t)xTaskGetTickCount();

            err = NF2FS_dir_gc(NF2FS, dir);
            if (err)
                return err;

            // // NEXT
            // uint32_t end = (uint32_t)xTaskGetTickCount();
            // printf("only gc time is %d\r\n", end - start);
        }

        // alloc a new sector if there still no enough space
        if (dir->tail_off + len >= NF2FS->cfg->sector_size) {
            err= NF2FS_sector_alloc(NF2FS, NF2FS->manager, NF2FS_SECTOR_DIR, 1,
                                       dir->tail_sector, dir->id, NF2FS_NULL, &dir->tail_sector, NULL);
            if (err)
                return err;
            dir->tail_off= sizeof(NF2FS_dir_sector_flash_t);

            // update the in-flash tail message
            err= NF2FS_dir_update(NF2FS, dir);
            if (err)
                return err;
        }
    }

    // directly prog if data is larger than normal cache size
    if (len >= NF2FS->cfg->cache_size) {
        // flush the pcache data
        err= NF2FS_cache_flush(NF2FS, NF2FS->pcache);
        if (err)
            return err;

        // directly prog data to flash
        err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_DHEAD, dir->tail_sector, 
                              dir->tail_off, len, buffer);
        if (err)
            return err;
    } else {
        // prog data to flash
        err= NF2FS_cache_prog(NF2FS, NF2FS->pcache, NF2FS->rcache, dir->tail_sector,
                            dir->tail_off, buffer, len);
        if (err)
            return err;   
    }
    dir->tail_off+= len;
    NF2FS_ASSERT(dir->tail_off <= NF2FS->cfg->sector_size);
    return err;
}

// set bits in erase map to old.
int NF2FS_dir_old(NF2FS_t *NF2FS, NF2FS_size_t tail)
{
    int err = NF2FS_ERR_OK;

    NF2FS_dir_sector_flash_t dsector_head;
    while (tail != NF2FS_NULL) {
        // read dir sector head
        err= NF2FS_direct_read(NF2FS, tail, 0, sizeof(NF2FS_dir_sector_flash_t),
                              &dsector_head);
        if (err)
            return err;

        // Turn state of sector head to old.
        dsector_head.head&= NF2FS_SHEAD_OLD_SET;
        err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_SHEAD, tail, 0,
                              sizeof(NF2FS_head_t), &dsector_head.head);
        if (err)
            return err;

        // Turn erase map to reuse.
        err = NF2FS_emap_set(NF2FS, NF2FS->manager, tail, 1);
        if (err)
            return err;
        tail = dsector_head.pre_sector;
    }
    return err;
}

// Update dir entry from its father dir.
int NF2FS_dir_update(NF2FS_t *NF2FS, NF2FS_dir_ram_t *dir)
{
    int err= NF2FS_ERR_OK;

    NF2FS_dir_name_flash_t *dir_name = NULL;
    NF2FS_dir_ram_t *father_dir = NULL;
    NF2FS_size_t len;

    // Find dir's corresponding entry.
    NF2FS_size_t entry_index;
    err = NF2FS_tree_entry_id_find(NF2FS->ram_tree, dir->id, &entry_index);
    if (err)
        return err;

    // update tree entry
    NF2FS->ram_tree->tree_array[entry_index].tail_sector= dir->tail_sector;

    // root dir do not need to update
    if (dir->father_id == NF2FS_ID_SUPER && dir->id == NF2FS_ID_ROOT) {
        // delete old root dir name in superblock
        err= NF2FS_data_delete(NF2FS, NF2FS_ID_SUPER, dir->name_sector, dir->name_off, sizeof(NF2FS_dir_name_flash_t));
        if (err)
            return err;

        // prog new root dir name in superblock
        NF2FS_dir_name_flash_t temp_dir= {
            .head= NF2FS_MKDHEAD(0, 1, NF2FS_ID_ROOT, NF2FS_DATA_DIR_NAME, sizeof(NF2FS_dir_name_flash_t)),
            .tail= dir->tail_sector,
        };
        err= NF2FS_prog_in_superblock(NF2FS, NF2FS->superblock, &temp_dir, sizeof(NF2FS_dir_name_flash_t));
        if (err)
            return err;

        // update ram messages, namelen of root dir is 0
        dir->name_sector = NF2FS->superblock->sector;
        dir->name_off= NF2FS->superblock->free_off - len;
        dir->namelen= 0;
        NF2FS->ram_tree->tree_array[entry_index].name_sector= dir->name_sector;
        NF2FS->ram_tree->tree_array[entry_index].name_off= dir->name_off;
        return err;
    }

    // Find father dir in tree.
    NF2FS_size_t father_index= NF2FS_NULL;
    err= NF2FS_tree_entry_id_find(NF2FS->ram_tree, dir->father_id, &father_index);
    if (err)
        return err;

    // open father dir
    NF2FS_tree_entry_ram_t* father_entry= &NF2FS->ram_tree->tree_array[father_index];
    err= NF2FS_dir_lowopen(NF2FS, father_entry->tail_sector, father_entry->id, father_entry->father_id,
                          father_entry->name_sector, father_entry->name_off, &father_dir, NF2FS->rcache);
    if (err)
        return err;

    // Read origin data to read cache.
    len = sizeof(NF2FS_dir_name_flash_t) + dir->namelen;
    dir_name= NF2FS_malloc(len);
    if (dir_name == NULL)
        return NF2FS_ERR_NOMEM;

    // read origin name to flash and prog a new one
    err= NF2FS_direct_read(NF2FS, dir->name_sector, dir->name_off, len, dir_name);
    if (err)
        goto cleanup;
    dir_name->head= NF2FS_MKDHEAD(0, 1, dir->id, NF2FS_DATA_DIR_NAME, len);
    dir_name->tail= dir->tail_sector;
    NF2FS_ASSERT(len == NF2FS_dhead_dsize(dir_name->head));

    // Set origin data to delete.
    err = NF2FS_data_delete(NF2FS, dir->father_id, dir->name_sector, dir->name_off, len);
    if (err)
        goto cleanup;

    // Prog new data.
    err = NF2FS_dir_prog(NF2FS, father_dir, dir_name, len);
    if (err)
        goto cleanup;

    NF2FS_cache_one(NF2FS, NF2FS->rcache);

    // Update address, the size of namelen is unchanged
    dir->name_sector = father_dir->tail_sector;
    dir->name_off= father_dir->tail_off - len;

    NF2FS->ram_tree->tree_array[entry_index].name_sector= dir->name_sector;
    NF2FS->ram_tree->tree_array[entry_index].name_off= dir->name_off;

cleanup:
    if (dir_name != NULL)
        NF2FS_free(dir_name);
    return err;
}

// open dir
int NF2FS_dir_lowopen(NF2FS_t* NF2FS, NF2FS_size_t tail, NF2FS_size_t id, NF2FS_size_t father_id,
                     NF2FS_size_t name_sector, NF2FS_size_t name_off, NF2FS_dir_ram_t **dir_addr,
                     NF2FS_cache_ram_t* cache)
{
    int err = NF2FS_ERR_OK;

    // If dir is opened, return
    NF2FS_dir_ram_t *dir = NF2FS->dir_list;
    while (dir != NULL) {
        if (dir->id == id) {
            *dir_addr = dir;
            return err;
        }
        dir = dir->next_dir;
    }

    // If not find, allocate memory for the dir.
    dir = NF2FS_malloc(sizeof(NF2FS_dir_ram_t));
    if (!dir)
        return NF2FS_ERR_NOMEM;

    // Initialize basic data for the dir.
    dir->id = id;
    dir->father_id = father_id;
    dir->name_sector = name_sector;
    dir->name_off= name_off;

    // get namelen from flash
    NF2FS_head_t head= NF2FS_NULL;
    err= NF2FS_direct_read(NF2FS, name_sector, name_off, sizeof(NF2FS_head_t), &head);
    if (err)
        return err;
    err= NF2FS_dhead_check(head, dir->id, NF2FS_NULL);
    if (err)
        return err;
    dir->namelen= NF2FS_dhead_dsize(head) - sizeof(NF2FS_dir_name_flash_t);
    
    dir->pos_sector = NF2FS_NULL;
    dir->pos_off = NF2FS_NULL;
    dir->pos_presector = NF2FS_NULL;

    // cal old space in the dir
    err = NF2FS_dtraverse_ospace(NF2FS, dir, tail, cache);
    if (err)
        goto cleanup;

    // Add dir to dir list.
    dir->next_dir = NF2FS->dir_list;
    NF2FS->dir_list = dir;
    *dir_addr = dir;
    return err;

cleanup:
    NF2FS_free(dir);
    return err;
}

// find opened dir with its id.
int NF2FS_open_dir_find(NF2FS_t* NF2FS, NF2FS_size_t id, NF2FS_dir_ram_t** dir_addr)
{
    NF2FS_dir_ram_t *fdir = NF2FS->dir_list;
    while (fdir != NULL && fdir->id != id)
        fdir = fdir->next_dir;

    if (fdir == NULL)
        return NF2FS_ERR_NODIROPEN;

    *dir_addr = fdir;
    return NF2FS_ERR_OK;
}

// create a new dir
int NF2FS_create_dir(NF2FS_t* NF2FS, NF2FS_dir_ram_t* father_dir, NF2FS_dir_ram_t** dir_addr, char* name, NF2FS_size_t namelen)
{
    int err = NF2FS_ERR_OK;

    NF2FS_size_t size;

    // Create in-ram dir structure.
    NF2FS_dir_name_flash_t *dir_name;
    NF2FS_dir_ram_t *dir = *dir_addr;
    dir = NF2FS_malloc(sizeof(NF2FS_dir_ram_t));
    if (dir == NULL)
        return NF2FS_ERR_NOMEM;

    // Allocate id for new dir.
    err = NF2FS_id_alloc(NF2FS, &dir->id);
    if (err)
        goto cleanup;

    // Allocate a sector for new dir.
    err = NF2FS_sector_alloc(NF2FS, NF2FS->manager, NF2FS_SECTOR_DIR, 1, NF2FS_NULL,
                              dir->id, father_dir->id, &dir->tail_sector, NULL);
    if (err)
        goto cleanup;

    // Allocate memory for in-flash dir name structure.
    size = sizeof(NF2FS_dir_name_flash_t) + namelen;
    dir_name = NF2FS_malloc(size);
    if (!dir_name) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Initialize data to dir name structure.
    dir_name->head = NF2FS_MKDHEAD(0, 1, dir->id, NF2FS_DATA_NDIR_NAME, size);
    dir_name->tail = dir->tail_sector;
    memcpy(dir_name->name, name, namelen);

    // Prog it to father dir.
    err = NF2FS_dir_prog(NF2FS, father_dir, dir_name, size);
    if (err)
        goto cleanup;

    // Update in-ram dir structure.
    dir->father_id= father_dir->id;
    
    dir->old_space= 0;
    dir->old_sector= NF2FS_NULL;
    dir->old_off= NF2FS_NULL;

    dir->pos_sector = NF2FS_NULL;
    dir->pos_off = NF2FS_NULL;
    dir->pos_presector= NF2FS_NULL;

    dir->name_sector = father_dir->tail_sector;
    dir->name_off= father_dir->tail_off - size;
    dir->namelen= namelen;

    dir->tail_off= sizeof(NF2FS_dir_sector_flash_t);

    // Add dir to dir list.
    dir->next_dir = NF2FS->dir_list;
    NF2FS->dir_list= dir;

    // Create new in-ram dir entry.
    err= NF2FS_tree_entry_add(NF2FS->ram_tree, father_dir->id, dir->id,
                             dir->name_sector, dir->name_off, dir->tail_sector, name, namelen);
    if (err)
        goto cleanup;

    // Return the new in-ram dir structure.
    *dir_addr = dir;
    NF2FS_free(dir_name);
    return err;

cleanup:
    if (dir)
        NF2FS_free(dir);
    if (dir_name)
        NF2FS_free(dir_name);
    return err;
}
