/**
 * The high level operations of N2FS
 */
#include "N2FS.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "N2FS_dir.h"
#include "N2FS_file.h"
#include "N2FS_head.h"
#include "N2FS_manage.h"
#include "N2FS_rw.h"
#include "N2FS_util.h"
#include "N2FS_tree.h"

/**
 * -------------------------------------------------------------------------------------------------------
 * --------------------------------------    Basic operations    --------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// free ram resources when deinit
void N2FS_deinit(N2FS_t *N2FS)
{
    // Free superblock.
    if (N2FS->superblock)
        N2FS_free(N2FS->superblock);

    // Free manager.
    N2FS_manager_free(N2FS->manager);

    // Free hash tree.
    if (N2FS->ram_tree) {
        if (N2FS->ram_tree->tree_array)
            N2FS_free(N2FS->ram_tree->tree_array);
        N2FS_free(N2FS->ram_tree);
    }

    // Free id map.
    N2FS_idmap_free(N2FS->id_map);

    // Free file list.
    N2FS_file_ram_t* file= N2FS->file_list;
    N2FS_file_ram_t* cur_file= NULL;
    while (file != NULL) {
        // printf("file is %d\n", file->id);
        cur_file= file;
        file= file->next_file;
        N2FS_free(cur_file->file_cache.buffer);
        N2FS_free(cur_file);
    }


    // Free dir list.
    N2FS_dir_ram_t* dir= N2FS->dir_list;
    N2FS_dir_ram_t* cur_dir= N2FS->dir_list;
    while (dir != NULL) {
        cur_dir= dir;
        dir= dir->next_dir;
        N2FS_free(cur_dir);
    }

    // Free read cache.
    if (N2FS->rcache) {
        if (N2FS->rcache->buffer)
            N2FS_free(N2FS->rcache->buffer);
        N2FS_free(N2FS->rcache);
    }

    // Free prog cache.
    if (N2FS->pcache) {
        if (N2FS->pcache->buffer)
            N2FS_free(N2FS->pcache->buffer);
        N2FS_free(N2FS->pcache);
    }
}

// Init ram structures when mount/format
int N2FS_init(N2FS_t *N2FS, const struct N2FS_config *cfg)
{
    N2FS->cfg = cfg;
    int err = 0;

    // make sure the size are valid
    N2FS_ASSERT(N2FS->cfg->read_size > 0);
    N2FS_ASSERT(N2FS->cfg->prog_size > 0);
    N2FS_ASSERT(N2FS->cfg->sector_size > 0);
    N2FS_ASSERT(N2FS->cfg->cache_size > 0);

    // make sure size is aligned
    N2FS_ASSERT(N2FS->cfg->cache_size % N2FS->cfg->read_size == 0);
    N2FS_ASSERT(N2FS->cfg->cache_size % N2FS->cfg->prog_size == 0);
    N2FS_ASSERT(N2FS->cfg->sector_size % N2FS->cfg->cache_size == 0);

    // make sure cfg satisfy restrictions
    N2FS_ASSERT(N2FS->cfg->region_cnt > 0);
    N2FS_ASSERT(N2FS->cfg->region_cnt <= N2FS_REGION_NUM_MAX);
    N2FS_ASSERT(N2FS->cfg->sector_count > 0);
    N2FS_ASSERT(N2FS->cfg->sector_count % N2FS->cfg->region_cnt == 0);
    N2FS_ASSERT(N2FS->cfg->name_max <= N2FS_NAME_MAX);
    N2FS_ASSERT(N2FS->cfg->file_max <= N2FS_FILE_MAX_SIZE);

    // init prog cache
    err = N2FS_cache_init(N2FS, &N2FS->pcache, N2FS->cfg->cache_size);
    if (err)
        goto cleanup;

    // Initialize read cache.
    err = N2FS_cache_init(N2FS, &N2FS->rcache, N2FS->cfg->cache_size);
    if (err)
        goto cleanup;

    // Initialize superblock message.
    err = N2FS_super_init(N2FS, &N2FS->superblock);
    if (err)
        goto cleanup;

    // Initialize manager structure.
    err = N2FS_manager_init(N2FS, &N2FS->manager);
    if (err)
        goto cleanup;

    // Initialize hash tree structure.
    err = N2FS_tree_init(N2FS, &N2FS->ram_tree);
    if (err)
        goto cleanup;

    // Initialize id map structure.
    err = N2FS_idmap_init(N2FS, &N2FS->id_map);
    if (err)
        goto cleanup;

    // init file list and dir list.
    N2FS->file_list= NULL;
    N2FS->dir_list= NULL;
    return err;

cleanup:
    N2FS_deinit(N2FS);
    return err;
}

// choose the right supersector when mounting
int N2FS_select_supersector(N2FS_t *N2FS, N2FS_size_t *sector)
{
    int err = N2FS_ERR_OK;
    N2FS_head_t superhead[2];

    // Read sector head of sector 0.
    err= N2FS_direct_read(N2FS, 0, 0, sizeof(N2FS_head_t), &superhead[0]);
    if (err)
        return err;

    // Read sector head of sector 0.
    err= N2FS_direct_read(N2FS, 1, 0, sizeof(N2FS_head_t), &superhead[1]);
    if (err)
        return err;

    // if some of super heads do not have data, return directly
    if (superhead[0] == N2FS_NULL && superhead[1] == N2FS_NULL) {
        return N2FS_ERR_NODATA;
    } else if (superhead[1] == N2FS_NULL) {
        *sector = 0;
        return err;
    } else if (superhead[0] == N2FS_NULL) {
        *sector = 1;
        return err;
    }

    // if one of the head is not valid, then something wrong
    if (N2FS_shead_check(superhead[0], N2FS_STATE_USING, N2FS_SECTOR_SUPER) ||
        N2FS_shead_check(superhead[1], N2FS_STATE_USING, N2FS_SECTOR_SUPER)) {
        // TODO in the future, corrupt happends, maybe the superblock gc.
        return N2FS_ERR_CORRUPT;
    }

    // choose the max extend number as valid super sector
    if (N2FS_shead_extend(superhead[0]) < N2FS_shead_extend(superhead[1])) {
        // special case that 0x3f + 0x1 -> 0x00 (extend only has 6 bits)
        if (N2FS_shead_extend(superhead[0]) == 0 &&
            N2FS_shead_extend(superhead[1]) == 0x3f) {
            *sector= 0;
            return err;
        }             
        *sector = 1;
        return err;
    }

    *sector = 0;
    return err;
}

/**
 * -------------------------------------------------------------------------------------------------------
 * --------------------------------------    FS level operations    --------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// format before first mounting
int N2FS_format(N2FS_t *N2FS, const struct N2FS_config *cfg, bool init_flag)
{
    int err = N2FS_ERR_OK;
    N2FS_size_t begin= N2FS_NULL;
    N2FS_size_t temp[4];
    N2FS_size_t temp_id;

    // malloc memory for ram structures
    if (init_flag) {
        err= N2FS_init(N2FS, cfg);
        if (err)
            goto cleanup;
    }

    // init basic ram manager message
    N2FS->manager->region_num= N2FS->cfg->region_cnt;
    N2FS->manager->region_size= N2FS->cfg->sector_count / N2FS->cfg->region_cnt;
    N2FS->manager->scan_times= 0;

    // currently assign sector 2 to sector map
    N2FS->manager->smap_begin= 2;
    N2FS->manager->smap_off= 0;
    N2FS->manager->etimes[0]= 0;

    // init region map,
    // region 0 is meta region, region 1 dir region
    N2FS->manager->region_map->begin= N2FS_NULL;
    N2FS->manager->region_map->reserve= 0;

    // malloc region for meta region and dir region
    err= N2FS_sector_nextsmap(N2FS, N2FS->manager, N2FS_SECTOR_META);
    if (err)
        goto cleanup;
    err= N2FS_sector_nextsmap(N2FS, N2FS->manager, N2FS_SECTOR_DIR);
    if (err)
        goto cleanup;

    // get the first three sectors for superblock, sector map, and id map
    // temp[4] is used to get erase times of three sectors
    err= N2FS_sector_alloc(N2FS, N2FS->manager, N2FS_SECTOR_MAP, 4,
                           N2FS_NULL, N2FS_NULL, N2FS_NULL, &begin, temp);
    if (err)
        return err;
    N2FS_ASSERT(begin == 0);
    
    // init the tree entry for root dir
    N2FS->ram_tree->tree_array[0].id= N2FS_ID_ROOT;
    N2FS->ram_tree->tree_array[0].father_id= N2FS_ID_SUPER;
    N2FS->ram_tree->tree_array[0].name_sector= N2FS_NULL;
    err= N2FS_sector_alloc(N2FS, N2FS->manager, N2FS_SECTOR_DIR, 1, N2FS_NULL, N2FS_ID_ROOT, N2FS_ID_SUPER,
                           &N2FS->ram_tree->tree_array[0].tail_sector, NULL);
    if (err)
        goto cleanup;

    // init the in-ram root dir
    N2FS->dir_list= N2FS_malloc(sizeof(N2FS_dir_ram_t));
    if (!N2FS->dir_list)
        goto cleanup;
    N2FS->dir_list->id= N2FS_ID_ROOT;
    N2FS->dir_list->father_id= N2FS_ID_SUPER;
    N2FS->dir_list->old_space= 0;
    N2FS->dir_list->old_sector= N2FS_NULL;
    N2FS->dir_list->name_sector= N2FS_NULL;
    N2FS->dir_list->pos_sector= N2FS_NULL;
    N2FS->dir_list->tail_sector= N2FS->ram_tree->tree_array[0].tail_sector;
    N2FS->dir_list->tail_off= sizeof(N2FS_dir_sector_flash_t);

    // init the id map
    N2FS->id_map->begin= 3;
    N2FS->id_map->off= 0;
    N2FS->id_map->etimes= 0;
    N2FS->id_map->ids_in_buffer= N2FS->manager->region_size;
    err= N2FS_ram_map_change(N2FS, 0, N2FS->id_map->ids_in_buffer, N2FS->id_map->free_map,
                             N2FS->id_map->begin, N2FS->id_map->off);
    if (err)
        goto cleanup;
    N2FS_ASSERT(N2FS->id_map->free_map->free_num == N2FS->id_map->ids_in_buffer);

    // id 0 and 1 are allocated for super and root dir
    for (int i= 0; i < 2; i++) {
        err= N2FS_id_alloc(N2FS, &temp_id);
        if (err)
            goto cleanup;
        N2FS_ASSERT(temp_id == i);
    }

    // prog basic message to superblock
    err= N2FS_superblock_change(N2FS, N2FS->superblock, N2FS->pcache, false);
    if (err)
        goto cleanup;
    return err;

cleanup:
    if (N2FS->dir_list)
        N2FS_free(N2FS->dir_list);
    N2FS_deinit(N2FS);
    return err;
}

// mount N2FS
int N2FS_mount(N2FS_t* N2FS, const struct N2FS_config* cfg)
{
    int err = N2FS_ERR_OK;

    // Init ram structures
    err = N2FS_init(N2FS, cfg);
    if (err)
        return err;

    // select the right superblock (0 or 1) to use
    N2FS->superblock->free_off = sizeof(N2FS_head_t);
    err = N2FS_select_supersector(N2FS, &N2FS->superblock->sector);
    if (err == N2FS_ERR_NODATA) {
        err= N2FS_format(N2FS, cfg, false);
        if (err)
            goto cleanup;
        else
            return err;
    } else if (err) {
        goto cleanup;
    }

    // Read data in superblock.
    while (true) {
        // Read to rcache
        N2FS_size_t size = N2FS_min(N2FS->cfg->cache_size,
                          N2FS->cfg->sector_size - N2FS->superblock->free_off);
        err= N2FS_read_to_cache(N2FS, N2FS->rcache, N2FS->superblock->sector,
                                N2FS->superblock->free_off, size);
        if (err)
            goto cleanup;
        uint8_t *data = N2FS->rcache->buffer;

        while (true) {
            N2FS_head_t head = *(N2FS_head_t *)data;
            N2FS_size_t len= N2FS_dhead_dsize(head);

            // Check if the head is valid.
            err = N2FS_dhead_check(head, N2FS_NULL, (int)N2FS_NULL);
            if (err)
                goto cleanup;

            if (N2FS->superblock->free_off + N2FS_dhead_dsize(head) > N2FS->rcache->off + size) {
                if (head == N2FS_NULL) {
                    // TODO in the future
                    // something may wrong during superblock gc.
                    N2FS_ERROR("Wrong in N2FS_mount\r\n");
                    err = N2FS_ERR_CORRUPT;
                    goto cleanup;
                }
                break;
            }
            
            // Check the data head type.
            switch (N2FS_dhead_type(head)) {
                case N2FS_DATA_SUPER_MESSAGE: {
                // check if N2FS message is true, size is 36B
                N2FS_supermessage_flash_t *message = (N2FS_supermessage_flash_t *)data;
                if (!memcpy(message->fs_name, &N2FS_FS_NAME, strlen(N2FS_FS_NAME)) ||
                    N2FS_VERSION != message->version ||
                    N2FS->cfg->sector_size != message->sector_size ||
                    N2FS->cfg->sector_count != message->sector_count ||
                    N2FS->cfg->name_max != message->name_max ||
                    N2FS->cfg->file_max != message->file_max ||
                    N2FS->cfg->region_cnt != message->region_cnt) {
                    err = N2FS_ERR_WRONGCFG;
                    goto cleanup;
                }
                break;
            }

            case N2FS_DATA_ID_MAP: {
                // update id map message
                N2FS_mapaddr_flash_t *flash_map = (N2FS_mapaddr_flash_t *)data;
                err = N2FS_idmap_assign(N2FS, N2FS->id_map, flash_map);
                if (err)
                    goto cleanup;
                break;
            }

            case N2FS_DATA_SECTOR_MAP: {
                // update sector map message
                N2FS_mapaddr_flash_t* flash_map= (N2FS_mapaddr_flash_t*)data;
                N2FS_size_t num= N2FS_alignup(N2FS->cfg->sector_count * 2 / 8, N2FS->cfg->sector_size) /
                                 N2FS->cfg->sector_size;
                err= N2FS_smap_assign(N2FS, N2FS->manager, flash_map, num);
                if (err)
                    goto cleanup;
                break;
            }

            case N2FS_DATA_WL_ADDR: {
                // update WL message
                err= N2FS_wl_init(N2FS, &N2FS->manager->wl);
                if (err)
                    return err;
                N2FS_wladdr_flash_t *wladdr = (N2FS_wladdr_flash_t *)data;
                N2FS->manager->wl->begin= wladdr->begin;
                N2FS->manager->wl->off= wladdr->off;
                N2FS->manager->wl->etimes= wladdr->erase_times;
                // TODO in the future, should update other wl message
                break;
            }

            case N2FS_DATA_DIR_NAME:
            case N2FS_DATA_NDIR_NAME: {
                // update root dir tree entry
                N2FS_ASSERT(N2FS_dhead_id(head) == N2FS_ID_ROOT);
                N2FS_dir_name_flash_t* name= (N2FS_dir_name_flash_t*)data;
                err= N2FS_tree_entry_add(N2FS->ram_tree, N2FS_ID_SUPER, N2FS_ID_ROOT, N2FS->superblock->sector,
                                         N2FS->superblock->free_off, name->tail, (char *)name->name, 0);
                if (err)
                    goto cleanup;

                // add open root dir to dir list
                N2FS_dir_ram_t *root_dir= NULL;
                err= N2FS_dir_lowopen(N2FS, name->tail, N2FS_ID_ROOT, N2FS_ID_SUPER, N2FS->superblock->sector,
                                      N2FS->superblock->free_off, &root_dir, N2FS->pcache);
                if (err)
                    goto cleanup;
                break;
            }

            case N2FS_DATA_REGION_MAP: {
                // region map message, size is 20B
                N2FS_region_map_flash_t* region_map= (N2FS_region_map_flash_t*)data;
                N2FS_region_map_assign(N2FS, N2FS->manager->region_map, region_map, N2FS->superblock->sector,
                                       N2FS->superblock->free_off);
                break;
            }

            case N2FS_DATA_COMMIT: {
                // update with commit message
                N2FS_commit_flash_t* commit= (N2FS_commit_flash_t*)data;
                err= N2FS_init_with_commit(N2FS, commit, N2FS->pcache);
                if (err)
                    goto cleanup;

                // set the commit message to delete, if do not have it, corrupt happens
                N2FS_head_validate(N2FS, N2FS->superblock->sector,
                                   N2FS->superblock->free_off, N2FS_DHEAD_DELETE_SET);

                N2FS->superblock->free_off+= len;
                return err;
            }

            case N2FS_DATA_DELETE:
                // Just skip, no need to do anything.
                break;

            case N2FS_DATA_FREE:
                // fail to find valid commit message, corrupt happens
                err= N2FS_ERR_CORRUPT;
                goto cleanup;

            default:
                // Other type of data head shouldn't be read, so there has been some problem.
                err = N2FS_ERR_CORRUPT;
                goto cleanup;
            }

            N2FS->superblock->free_off += len;
            data += len;

            // the next data head is not entire, read again
            if (N2FS->superblock->free_off - N2FS->rcache->off + sizeof(N2FS_head_t) >= N2FS->cfg->cache_size)
                break;
        }
    }

cleanup:
    N2FS_deinit(N2FS);
    return err;
}

// unmount N2FS
int N2FS_unmount(N2FS_t *N2FS)
{
    int err = N2FS_ERR_OK;

    // Flush files
    N2FS_file_ram_t *file = N2FS->file_list;
    while (file != NULL) {
        err = N2FS_file_flush(N2FS, file);
        if (err)
            return err;
        file = file->next_file;
    }

    // flush dir
    N2FS_dir_ram_t *dir = N2FS->dir_list;
    while (dir != NULL) {
        // record old space in dir
        N2FS_dir_ospace_flash_t old_space= {
            .head= N2FS_MKDHEAD(0, 1, dir->id, N2FS_DATA_DIR_OSPACE, sizeof(N2FS_dir_ospace_flash_t)),
            .old_space= dir->old_space,
        };

        err= N2FS_dir_prog(N2FS, dir, &old_space, sizeof(N2FS_dir_ospace_flash_t));
        if (err)
            return err;
        dir = dir->next_dir;
    }

    // Flush region map to flash.
    err = N2FS_region_map_flush(N2FS, N2FS->manager->region_map);
    if (err)
        return err;

    // Prog new commit message.
    N2FS_commit_flash_t commit= {
        .head= N2FS_MKDHEAD(0, 1, N2FS_ID_SUPER, N2FS_DATA_COMMIT, sizeof(N2FS_commit_flash_t)),
        .next_id= N2FS->id_map->free_map->region * N2FS->id_map->ids_in_buffer + N2FS->id_map->free_map->index_or_changed,
        .scan_times= N2FS->manager->scan_times,
        .next_dir_sector= N2FS->manager->dir_map->region * N2FS->manager->region_size + N2FS->manager->dir_map->index_or_changed,
        .next_bfile_sector= N2FS->manager->bfile_map->region * N2FS->manager->region_size + N2FS->manager->bfile_map->index_or_changed,
        .reserve_region= N2FS->manager->region_map->reserve,
    };

    err= N2FS_prog_in_superblock(N2FS, N2FS->superblock, &commit, sizeof(N2FS_commit_flash_t));
    if (err)
        return err;

    // Flush free dir map to flash.
    err= N2FS_smap_flush(N2FS, N2FS->manager);
    if (err)
        return err;

    // Flush data in pcache to flash.
    err = N2FS_cache_flush(N2FS, N2FS->pcache);
    if (err)
        return err;

    // Free all in-ram structre's memory.
    N2FS_deinit(N2FS);
    return err;
}

/**
 * -------------------------------------------------------------------------------------------------------
 * -------------------------------------    File level operations    -------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// open a file
int N2FS_file_open(N2FS_t* N2FS, N2FS_file_ram_t** file, char* path, int flags)
{
    int err = N2FS_ERR_OK;

    // check whether open file is too much
    N2FS_file_ram_t* temp_file= N2FS->file_list;
    N2FS_size_t cnt= 0;
    while (temp_file != NULL) {
        cnt++;
        temp_file= temp_file->next_file;
    }
    if (cnt >= N2FS_FILE_LIST_MAX)
        return N2FS_ERR_MUCHOPEN;

    // find the father dir
    N2FS_tree_entry_ram_t *entry = NULL;
    err= N2FS_father_dir_find(N2FS, path, &entry);
    if (err)
        return err;
    N2FS_ASSERT(entry->tail_sector != N2FS_NULL);

    // Open father dir of file.
    N2FS_dir_ram_t* father_dir= NULL;
    err= N2FS_dir_lowopen(N2FS, entry->tail_sector, entry->id, entry->father_id,
                          entry->name_sector, entry->name_off, &father_dir, N2FS->rcache);
    if (err)
        return err;

    // Find The true name of file in path.
    char *name = N2FS_name_in_path(path);
    if (*name == '/')
        name++;

    // Traverse dir to find whether or not the file is already in dir.
    N2FS_tree_entry_ram_t temp_entry;
    err= N2FS_dtraverse_name(N2FS, father_dir->tail_sector, name, strlen(name),
                             N2FS_DATA_REG, &temp_entry);
    if (err)
        return err;

    if (temp_entry.id != N2FS_NULL) {
        // find the file name, open it
        err= N2FS_file_lowopen(N2FS, father_dir, temp_entry.id, temp_entry.name_sector,
                               temp_entry.name_off, strlen(name), file);
        if (err)
            return err;
    } else {
        // If not find, we should create a new file.
        err = N2FS_create_file(N2FS, father_dir, file, name, strlen(name));
        if (err)
            return err;
    }
    return err;
}

// close a file
int N2FS_file_close(N2FS_t* N2FS, N2FS_file_ram_t* file)
{
    int err = N2FS_ERR_OK;
    
    // flush data to flash first
    err = N2FS_file_flush(N2FS, file);
    if (err)
        return err;

    // free the file
    err= N2FS_file_free(&N2FS->file_list, file);
    return err;
}

// read data of a file
int N2FS_file_read(N2FS_t* N2FS, N2FS_file_ram_t* file, void* buffer, N2FS_size_t size)
{
    // error if read size is larger than file size
    if (file->file_pos + size > file->file_size) {
        N2FS_ERROR("file message wrong before reading\r\n");
        return N2FS_ERR_INVAL;
    }

    if (file->file_size <= N2FS_FILE_SIZE_THRESHOLD) {
        // Small file read function.
        return N2FS_small_file_read(N2FS, file, buffer, size);
    } else {
        // big file read function
        return N2FS_big_file_read(N2FS, file, buffer, size);
    }
}

// write data to a file
int N2FS_file_write(N2FS_t* N2FS, N2FS_file_ram_t* file, void* buffer, N2FS_size_t size)
{
    // Error if file size is larger than max size after writing
    if (file->file_pos + size > N2FS->cfg->file_max) {
        return N2FS_ERR_FBIG;
    }

    if (file->file_size <= N2FS_FILE_SIZE_THRESHOLD &&
        file->file_pos + size <= N2FS_FILE_SIZE_THRESHOLD) {
        // prog small file
        return N2FS_small_file_write(N2FS, file, buffer, size);
    } else if (file->file_size >= 0 &&
               file->file_size <= N2FS_FILE_SIZE_THRESHOLD &&
               file->file_size + size > N2FS_FILE_SIZE_THRESHOLD) {
        // change small file to big file
        return N2FS_s2b_file_write(N2FS, file, buffer, size);
    } else if (file->file_size + size > N2FS_FILE_SIZE_THRESHOLD) {
        // prog data to a existing big file
        return N2FS_big_file_write(N2FS, file, buffer, size);
    }
    return N2FS_ERR_INVAL;
}

// change the file position
int N2FS_file_seek(N2FS_t* N2FS, N2FS_file_ram_t* file, N2FS_soff_t off, int whence)
{
    switch (whence)
    {
    case N2FS_SEEK_SET:
        // Absolute position
        if (off < 0 || off > file->file_size)
            return N2FS_ERR_INVAL;
        file->file_pos = off;
        break;

    case N2FS_SEEK_CUR:
        // Current + off;
        if ((N2FS_soff_t)file->file_pos + off < 0 ||
            (N2FS_soff_t)file->file_pos + off > file->file_size)
            return N2FS_ERR_INVAL;
        else
            file->file_pos = file->file_pos + off;
        break;

    case N2FS_SEEK_END: {
        // end position + off
        N2FS_soff_t res = file->file_size + off;
        if (res < 0 || res > file->file_size)
            return N2FS_ERR_INVAL;
        else
            file->file_pos = res;
        break;
    }

    default:
        return N2FS_ERR_INVAL;
    }
    return N2FS_ERR_OK;
}

// delete a file
int N2FS_file_delete(N2FS_t* N2FS, N2FS_file_ram_t* file)
{
    int err= N2FS_ERR_OK;

    // delete sectors belong to big file
    N2FS_head_t head= *(N2FS_head_t*)file->file_cache.buffer;
    N2FS_ASSERT(head != N2FS_NULL);
    if (N2FS_dhead_type(head) == N2FS_DATA_BFILE_INDEX) {
        // Because head in buffer may be old, we use size in file cache.
        N2FS_bfile_index_flash_t *index = (N2FS_bfile_index_flash_t *)file->file_cache.buffer;
        N2FS_size_t num = (file->file_cache.size - sizeof(N2FS_head_t)) / sizeof(N2FS_bfile_index_ram_t);
        err = N2FS_bfile_sector_old(N2FS, &index->index[0], num);
        if (err)
            return err;
    }

    // Delete small file's data or big file's index.
    err = N2FS_data_delete(N2FS, file->father_id, file->file_cache.sector,
                             file->file_cache.off, N2FS_dhead_dsize(head));
    if (err)
        return err;

    // Delete file's name in its father dir.
    err= N2FS_data_delete(N2FS, file->father_id, file->sector,
                          file->off, file->namelen + sizeof(N2FS_file_name_flash_t));
    if (err)
        return err;

    // Free id that the file belongs to.
    err = N2FS_id_free(N2FS, N2FS->id_map, file->id);
    if (err)
        return err;

    // Free in-ram memory of the file.
    err = N2FS_file_free(&N2FS->file_list, file);
    return err;
}

// flush file data to flash
int N2FS_file_sync(N2FS_t* N2FS, N2FS_file_ram_t* file)
{
    return N2FS_file_flush(N2FS, file);
}

/**
 * -------------------------------------------------------------------------------------------------------
 * -------------------------------------    Dir level operations    --------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// open a dir
int N2FS_dir_open(N2FS_t* N2FS, N2FS_dir_ram_t** dir, char* path)
{
    int err = N2FS_ERR_OK;

    // fail to open if opened dir is too much
    N2FS_dir_ram_t* temp_dir= N2FS->dir_list;
    N2FS_size_t cnt= 0;
    while (temp_dir != NULL) {
        cnt++;
        temp_dir= temp_dir->next_dir;
    }
    if (cnt >= N2FS_DIR_LIST_MAX)
        return N2FS_ERR_MUCHOPEN;

    // find father dir of the open dir
    N2FS_tree_entry_ram_t *father_entry = NULL;
    err= N2FS_father_dir_find(N2FS, path, &father_entry);
    if (err)
        return err;

    // Open father dir of dir.
    N2FS_dir_ram_t* father_dir= NULL;
    err= N2FS_dir_lowopen(N2FS, father_entry->tail_sector, father_entry->id, father_entry->father_id,
                          father_entry->name_sector, father_entry->name_off, &father_dir, N2FS->rcache);
    if (err)
        return err;

    // Find The true name of file in path.
    char *name = N2FS_name_in_path(path);
    if (*name == '/')
        name++;

    // find opened dir in ram-tree
    N2FS_size_t tree_index= N2FS_NULL;
    N2FS_tree_entry_ram_t temp_entry;
    err= N2FS_tree_entry_name_find(N2FS, name, strlen(name), father_dir->id, &tree_index);
    if (err) {
        // find opened dir in flash
        err= N2FS_dtraverse_name(N2FS, father_dir->tail_sector, name, strlen(name),
                                N2FS_DATA_REG, &temp_entry);
        if (err)
            return err;
    }

    if (tree_index == N2FS_NULL && temp_entry.id == N2FS_NULL) {
        // fail to find opened dir, create one
        err = N2FS_create_dir(N2FS, father_dir, dir, name, strlen(name));
        if (err)
            return err;
    } else {
        // find tree entry of the opened dir
        if (tree_index != N2FS_NULL) {
            memcpy(&temp_entry, &N2FS->ram_tree->tree_array[tree_index], sizeof(N2FS_tree_entry_ram_t));
        }
        N2FS_ASSERT(temp_entry.id != N2FS_NULL);

        // open the dir
        err= N2FS_dir_lowopen(N2FS, temp_entry.tail_sector, temp_entry.id, temp_entry.father_id,
                                  temp_entry.name_sector, temp_entry.name_off, dir, N2FS->rcache);
        if (err)
            return err;
    }
    return err;
}

// close a dir
int N2FS_dir_close(N2FS_t* N2FS, N2FS_dir_ram_t* dir)
{
    int err = N2FS_ERR_OK;

    // if file still opens, can not close dir.
    N2FS_file_ram_t* file= N2FS->file_list;
    while (file != NULL) {
        if (file->father_id == dir->id) {
            N2FS_ERROR("files are still opened, can not close dir!!\n");
            return N2FS_ERR_WRONGPROG;
        }
        file= file->next_file;
    }

    // record old space in dir
    N2FS_dir_ospace_flash_t old_space= {
        .head= N2FS_MKDHEAD(0, 1, dir->id, N2FS_DATA_DIR_OSPACE, sizeof(N2FS_dir_ospace_flash_t)),
        .old_space= dir->old_space,
    };
    err= N2FS_dir_prog(N2FS, dir, &old_space, sizeof(N2FS_dir_ospace_flash_t));
    if (err)
        return err;

    // flush cache data to flash
    err = N2FS_cache_flush(N2FS, N2FS->pcache);
    if (err)
        return err;

    // Delete it in dir list.
    N2FS_dir_ram_t *pre_dir = N2FS->dir_list;
    if (pre_dir->id == dir->id) {
        N2FS->dir_list = pre_dir->next_dir;
    } else {
        while (pre_dir->next_dir != NULL) {
            if (pre_dir->next_dir->id == dir->id)
                break;
            pre_dir = pre_dir->next_dir;
        }
        if (pre_dir == NULL)
            return N2FS_ERR_NODIROPEN;
        pre_dir->next_dir = dir->next_dir;
    }

    // free dir's memory
    N2FS_free(dir);
    return err;
}

// delete a dir
int N2FS_dir_delete(N2FS_t* N2FS, N2FS_dir_ram_t* dir)
{
    int err = N2FS_ERR_OK;

    // Find dir's father dir
    N2FS_dir_ram_t* father_dir= NULL;
    err= N2FS_open_dir_find(N2FS, dir->father_id, &father_dir);
    if (err)
        return err;

    // delete all sectors that belong to dir's son file
    err = N2FS_dtraverse_bfile_delete(N2FS, dir);
    if (err)
        return err;

    // Set all sectors belongs to dir to old.
    err = N2FS_dir_old(N2FS, dir->tail_sector);
    if (err)
        return err;

    // Delete dir name entry in father dir.
    err = N2FS_data_delete(N2FS, father_dir->id, dir->name_sector, dir->name_off,
                             sizeof(N2FS_dir_name_flash_t) + dir->namelen);
    if (err)
        return err;

    // Delete the tree entry
    err= N2FS_tree_entry_remove(N2FS->ram_tree, dir->id);
    if (err)
        return err;

    // Free id that belongs to dir.
    err = N2FS_id_free(N2FS, N2FS->id_map, dir->id);
    if (err)
        return err;

    // Free in-ram dir structure.
    err = N2FS_dir_free(N2FS->dir_list, dir);
    return err;
}

// read an dir entry from dir.
int N2FS_dir_read(N2FS_t* N2FS, N2FS_dir_ram_t* dir, N2FS_info_ram_t* info)
{
    int err = N2FS_ERR_OK;
    memset(info, 0, sizeof(*info));

    // If it's not initialized, we should do it.
    if (dir->pos_sector == N2FS_NULL) {
        dir->pos_sector = dir->tail_sector;
        dir->pos_off = 0;
        dir->pos_presector = N2FS_NULL;
    }

    uint8_t *data = NULL;
    N2FS_size_t head;
    N2FS_size_t len = sizeof(N2FS_head_t);
    while (true) {
        if (~((N2FS->rcache->sector == dir->pos_sector) &&
              (N2FS->rcache->off + N2FS->rcache->size >= dir->pos_off + len) &&
              (N2FS->rcache->off <= dir->pos_off))) {
            
            // read other data to cache
            err = N2FS_read_to_cache(N2FS, N2FS->rcache, dir->pos_sector, dir->pos_off,
                                       N2FS_min(N2FS->cfg->cache_size, N2FS->cfg->sector_size - dir->pos_off));
            if (err)
                return err;

            data = N2FS->rcache->buffer;
        } else {
            // get the next head position
            data = N2FS->rcache->buffer + dir->pos_off - N2FS->rcache->off;
        }

        if (dir->pos_off == 0) {
            // record sector message for traversing
            head = *(N2FS_head_t *)data;
            err = N2FS_shead_check(head, N2FS_STATE_USING, N2FS_SECTOR_DIR);
            if (err)
                return err;

            N2FS_dir_sector_flash_t *dir_sector = (N2FS_dir_sector_flash_t *)data;
            dir->pos_presector = dir_sector->pre_sector;
            data += sizeof(N2FS_dir_sector_flash_t);
            dir->pos_off += sizeof(N2FS_dir_sector_flash_t);
            len = sizeof(N2FS_head_t);
        }

        bool if_next = false;
        while (true) {
            head= *(N2FS_head_t*)data;
            
            // Check if the head is valid.
            err = N2FS_dhead_check(head, N2FS_NULL, (int)N2FS_NULL);
            if (err) {
                return err;
            }

            len = N2FS_dhead_dsize(head);
            if (dir->pos_off + len > N2FS->rcache->off + N2FS->rcache->size) {
                if (head == N2FS_NULL && dir->pos_presector != N2FS_NULL) {
                    // time to traverse next sector
                    dir->pos_sector = dir->pos_presector;
                    dir->pos_off = 0;
                    len = sizeof(N2FS_head_t);
                    break;
                } else if (head == N2FS_NULL && dir->pos_presector == N2FS_NULL) {
                    // finishing traversing
                    return err;
                }
                // reread data to cache
                break;
            }

            switch (N2FS_dhead_type(head))
            {
            case N2FS_DATA_NDIR_NAME:
            case N2FS_DATA_DIR_NAME: {
                N2FS_dir_name_flash_t *dname = (N2FS_dir_name_flash_t *)data;
                info->type = N2FS_DATA_DIR;
                memcpy(info->name, dname->name, len - sizeof(N2FS_dir_name_flash_t));
                info->name[len - sizeof(N2FS_dir_name_flash_t)] = '\0';
                dir->pos_off += len;
                return err;
            }

            case N2FS_DATA_NFILE_NAME:
            case N2FS_DATA_FILE_NAME: {
                N2FS_file_name_flash_t *fname = (N2FS_file_name_flash_t *)data;
                info->type = N2FS_DATA_REG;
                memcpy(info->name, fname->name, len - sizeof(N2FS_file_name_flash_t));
                info->name[len - sizeof(N2FS_file_name_flash_t)] = '\0';
                dir->pos_off += len;
                return err;
            }

            case N2FS_DATA_DELETE:
            case N2FS_DATA_BFILE_INDEX:
            case N2FS_DATA_SFILE_DATA:
            case N2FS_DATA_DIR_OSPACE:    
                break;

            case N2FS_DATA_FREE:
                N2FS_ASSERT(head == N2FS_NULL);
                if_next = true;
                break;

            default:
                break;
            }

            if (if_next) {
                if (dir->pos_presector == N2FS_NULL) {
                    // the end of readdir
                    info->type= N2FS_DATA_DELETE;
                    return err;
                }
                dir->pos_sector = dir->pos_presector;
                dir->pos_off = 0;
                break;
            } else {
                dir->pos_off += len;
                data += len;
                len= sizeof(N2FS_head_t);
                if (dir->pos_off + sizeof(N2FS_head_t) < N2FS->rcache->off + N2FS->rcache->size)
                    break;
            }
        }
    }
}
