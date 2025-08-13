/**
 * The high level operations of NF2FS
 */
#include "NF2FS.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "NF2FS_dir.h"
#include "NF2FS_file.h"
#include "NF2FS_head.h"
#include "NF2FS_manage.h"
#include "NF2FS_rw.h"
#include "NF2FS_util.h"
#include "NF2FS_tree.h"

/**
 * -------------------------------------------------------------------------------------------------------
 * --------------------------------------    Basic operations    --------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// free ram resources when deinit
void NF2FS_deinit(NF2FS_t *NF2FS)
{
    // Free superblock.
    if (NF2FS->superblock)
        NF2FS_free(NF2FS->superblock);

    // Free manager.
    NF2FS_manager_free(NF2FS->manager);

    // Free hash tree.
    if (NF2FS->ram_tree) {
        if (NF2FS->ram_tree->tree_array)
            NF2FS_free(NF2FS->ram_tree->tree_array);
        NF2FS_free(NF2FS->ram_tree);
    }

    // Free id map.
    NF2FS_idmap_free(NF2FS->id_map);

    // Free file list.
    NF2FS_file_ram_t* file= NF2FS->file_list;
    NF2FS_file_ram_t* cur_file= NULL;
    while (file != NULL) {
        // printf("file is %d\n", file->id);
        cur_file= file;
        file= file->next_file;
        NF2FS_free(cur_file->file_cache.buffer);
        NF2FS_free(cur_file);
    }


    // Free dir list.
    NF2FS_dir_ram_t* dir= NF2FS->dir_list;
    NF2FS_dir_ram_t* cur_dir= NF2FS->dir_list;
    while (dir != NULL) {
        cur_dir= dir;
        dir= dir->next_dir;
        NF2FS_free(cur_dir);
    }

    // Free read cache.
    if (NF2FS->rcache) {
        if (NF2FS->rcache->buffer)
            NF2FS_free(NF2FS->rcache->buffer);
        NF2FS_free(NF2FS->rcache);
    }

    // Free prog cache.
    if (NF2FS->pcache) {
        if (NF2FS->pcache->buffer)
            NF2FS_free(NF2FS->pcache->buffer);
        NF2FS_free(NF2FS->pcache);
    }
}

// Init ram structures when mount/format
int NF2FS_init(NF2FS_t *NF2FS, const struct NF2FS_config *cfg)
{
    NF2FS->cfg = cfg;
    int err = 0;

    // make sure the size are valid
    NF2FS_ASSERT(NF2FS->cfg->read_size > 0);
    NF2FS_ASSERT(NF2FS->cfg->prog_size > 0);
    NF2FS_ASSERT(NF2FS->cfg->sector_size > 0);
    NF2FS_ASSERT(NF2FS->cfg->cache_size > 0);

    // make sure size is aligned
    NF2FS_ASSERT(NF2FS->cfg->cache_size % NF2FS->cfg->read_size == 0);
    NF2FS_ASSERT(NF2FS->cfg->cache_size % NF2FS->cfg->prog_size == 0);
    NF2FS_ASSERT(NF2FS->cfg->sector_size % NF2FS->cfg->cache_size == 0);

    // make sure cfg satisfy restrictions
    NF2FS_ASSERT(NF2FS->cfg->region_cnt > 0);
    NF2FS_ASSERT(NF2FS->cfg->region_cnt <= NF2FS_REGION_NUM_MAX);
    NF2FS_ASSERT(NF2FS->cfg->sector_count > 0);
    NF2FS_ASSERT(NF2FS->cfg->sector_count % NF2FS->cfg->region_cnt == 0);
    NF2FS_ASSERT(NF2FS->cfg->name_max <= NF2FS_NAME_MAX);
    NF2FS_ASSERT(NF2FS->cfg->file_max <= NF2FS_FILE_MAX_SIZE);

    // init prog cache
    err = NF2FS_cache_init(NF2FS, &NF2FS->pcache, NF2FS->cfg->cache_size);
    if (err)
        goto cleanup;

    // Initialize read cache.
    err = NF2FS_cache_init(NF2FS, &NF2FS->rcache, NF2FS->cfg->cache_size);
    if (err)
        goto cleanup;

    // Initialize superblock message.
    err = NF2FS_super_init(NF2FS, &NF2FS->superblock);
    if (err)
        goto cleanup;

    // Initialize manager structure.
    err = NF2FS_manager_init(NF2FS, &NF2FS->manager);
    if (err)
        goto cleanup;

    // Initialize hash tree structure.
    err = NF2FS_tree_init(NF2FS, &NF2FS->ram_tree);
    if (err)
        goto cleanup;

    // Initialize id map structure.
    err = NF2FS_idmap_init(NF2FS, &NF2FS->id_map);
    if (err)
        goto cleanup;

    // init file list and dir list.
    NF2FS->file_list= NULL;
    NF2FS->dir_list= NULL;
    return err;

cleanup:
    NF2FS_deinit(NF2FS);
    return err;
}

// choose the right supersector when mounting
int NF2FS_select_supersector(NF2FS_t *NF2FS, NF2FS_size_t *sector)
{
    int err = NF2FS_ERR_OK;
    NF2FS_head_t superhead[2];

    // Read sector head of sector 0.
    err= NF2FS_direct_read(NF2FS, 0, 0, sizeof(NF2FS_head_t), &superhead[0]);
    if (err)
        return err;

    // Read sector head of sector 0.
    err= NF2FS_direct_read(NF2FS, 1, 0, sizeof(NF2FS_head_t), &superhead[1]);
    if (err)
        return err;

    // if some of super heads do not have data, return directly
    if (superhead[0] == NF2FS_NULL && superhead[1] == NF2FS_NULL) {
        return NF2FS_ERR_NODATA;
    } else if (superhead[1] == NF2FS_NULL) {
        *sector = 0;
        return err;
    } else if (superhead[0] == NF2FS_NULL) {
        *sector = 1;
        return err;
    }

    // if one of the head is not valid, then something wrong
    if (NF2FS_shead_check(superhead[0], NF2FS_STATE_USING, NF2FS_SECTOR_SUPER) ||
        NF2FS_shead_check(superhead[1], NF2FS_STATE_USING, NF2FS_SECTOR_SUPER)) {
        // TODO in the future, corrupt happends, maybe the superblock gc.
        return NF2FS_ERR_CORRUPT;
    }

    // choose the max extend number as valid super sector
    if (NF2FS_shead_extend(superhead[0]) < NF2FS_shead_extend(superhead[1])) {
        // special case that 0x3f + 0x1 -> 0x00 (extend only has 6 bits)
        if (NF2FS_shead_extend(superhead[0]) == 0 &&
            NF2FS_shead_extend(superhead[1]) == 0x3f) {
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
int NF2FS_format(NF2FS_t *NF2FS, const struct NF2FS_config *cfg, bool init_flag)
{
    int err = NF2FS_ERR_OK;
    NF2FS_size_t begin= NF2FS_NULL;
    NF2FS_size_t temp[4];
    NF2FS_size_t temp_id;

    // malloc memory for ram structures
    if (init_flag) {
        err= NF2FS_init(NF2FS, cfg);
        if (err)
            goto cleanup;
    }

    // init basic ram manager message
    NF2FS->manager->region_num= NF2FS->cfg->region_cnt;
    NF2FS->manager->region_size= NF2FS->cfg->sector_count / NF2FS->cfg->region_cnt;
    NF2FS->manager->scan_times= 0;

    // currently assign sector 2 to sector map
    NF2FS->manager->smap_begin= 2;
    NF2FS->manager->smap_off= 0;
    NF2FS->manager->etimes[0]= 0;

    // init region map,
    // region 0 is meta region, region 1 dir region
    NF2FS->manager->region_map->begin= NF2FS_NULL;
    NF2FS->manager->region_map->reserve= 0;

    // malloc region for meta region and dir region
    err= NF2FS_sector_nextsmap(NF2FS, NF2FS->manager, NF2FS_SECTOR_META);
    if (err)
        goto cleanup;
    err= NF2FS_sector_nextsmap(NF2FS, NF2FS->manager, NF2FS_SECTOR_DIR);
    if (err)
        goto cleanup;

    // get the first three sectors for superblock, sector map, and id map
    // temp[4] is used to get erase times of three sectors
    err= NF2FS_sector_alloc(NF2FS, NF2FS->manager, NF2FS_SECTOR_MAP, 4,
                           NF2FS_NULL, NF2FS_NULL, NF2FS_NULL, &begin, temp);
    if (err)
        return err;
    NF2FS_ASSERT(begin == 0);
    
    // init the tree entry for root dir
    NF2FS->ram_tree->tree_array[0].id= NF2FS_ID_ROOT;
    NF2FS->ram_tree->tree_array[0].father_id= NF2FS_ID_SUPER;
    NF2FS->ram_tree->tree_array[0].name_sector= NF2FS_NULL;
    err= NF2FS_sector_alloc(NF2FS, NF2FS->manager, NF2FS_SECTOR_DIR, 1, NF2FS_NULL, NF2FS_ID_ROOT, NF2FS_ID_SUPER,
                           &NF2FS->ram_tree->tree_array[0].tail_sector, NULL);
    if (err)
        goto cleanup;

    // init the in-ram root dir
    NF2FS->dir_list= NF2FS_malloc(sizeof(NF2FS_dir_ram_t));
    if (!NF2FS->dir_list)
        goto cleanup;
    NF2FS->dir_list->id= NF2FS_ID_ROOT;
    NF2FS->dir_list->father_id= NF2FS_ID_SUPER;
    NF2FS->dir_list->old_space= 0;
    NF2FS->dir_list->old_sector= NF2FS_NULL;
    NF2FS->dir_list->name_sector= NF2FS_NULL;
    NF2FS->dir_list->pos_sector= NF2FS_NULL;
    NF2FS->dir_list->tail_sector= NF2FS->ram_tree->tree_array[0].tail_sector;
    NF2FS->dir_list->tail_off= sizeof(NF2FS_dir_sector_flash_t);
    NF2FS->dir_list->next_dir = NULL;

    // init the id map
    NF2FS->id_map->begin= 3;
    NF2FS->id_map->off= 0;
    NF2FS->id_map->etimes= 0;
    NF2FS->id_map->ids_in_buffer= NF2FS->manager->region_size;
    err= NF2FS_ram_map_change(NF2FS, 0, NF2FS->id_map->ids_in_buffer, NF2FS->id_map->free_map,
                             NF2FS->id_map->begin, NF2FS->id_map->off);
    if (err)
        goto cleanup;
    NF2FS_ASSERT(NF2FS->id_map->free_map->free_num == NF2FS->id_map->ids_in_buffer);

    // id 0 and 1 are allocated for super and root dir
    for (int i= 0; i < 2; i++) {
        err= NF2FS_id_alloc(NF2FS, &temp_id);
        if (err)
            goto cleanup;
        NF2FS_ASSERT(temp_id == i);
    }

    // prog basic message to superblock
    err= NF2FS_superblock_change(NF2FS, NF2FS->superblock, NF2FS->pcache, false);
    if (err)
        goto cleanup;
    return err;

cleanup:
    if (NF2FS->dir_list)
        NF2FS_free(NF2FS->dir_list);
    NF2FS_deinit(NF2FS);
    return err;
}

// mount NF2FS
int NF2FS_mount(NF2FS_t* NF2FS, const struct NF2FS_config* cfg)
{
    int err = NF2FS_ERR_OK;
    
    // Init ram structures
    err = NF2FS_init(NF2FS, cfg);
    if (err)
        return err;

    // select the right superblock (0 or 1) to use
    NF2FS->superblock->free_off = sizeof(NF2FS_head_t);
    err = NF2FS_select_supersector(NF2FS, &NF2FS->superblock->sector);
    if (err == NF2FS_ERR_NODATA) {
        err= NF2FS_format(NF2FS, cfg, false);
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
        NF2FS_size_t size = NF2FS_min(NF2FS->cfg->cache_size,
                          NF2FS->cfg->sector_size - NF2FS->superblock->free_off);
        err= NF2FS_read_to_cache(NF2FS, NF2FS->rcache, NF2FS->superblock->sector,
                                NF2FS->superblock->free_off, size);
        if (err)
            goto cleanup;
        uint8_t *data = NF2FS->rcache->buffer;

        while (true) {
            NF2FS_head_t head = *(NF2FS_head_t *)data;
            NF2FS_size_t len= NF2FS_dhead_dsize(head);

            // Check if the head is valid.
            err = NF2FS_dhead_check(head, NF2FS_NULL, (int)NF2FS_NULL);
            if (err)
                goto cleanup;

            if (NF2FS->superblock->free_off + NF2FS_dhead_dsize(head) > NF2FS->rcache->off + size) {
                if (head == NF2FS_NULL) {
                    // TODO in the future
                    // something may wrong during superblock gc.
                    NF2FS_ERROR("Wrong in NF2FS_mount\r\n");
                    err = NF2FS_ERR_CORRUPT;
                    goto cleanup;
                }
                break;
            }
            
            // Check the data head type.
            switch (NF2FS_dhead_type(head)) {
                case NF2FS_DATA_SUPER_MESSAGE: {
                // check if NF2FS message is true, size is 36B
                NF2FS_supermessage_flash_t *message = (NF2FS_supermessage_flash_t *)data;
                if (!memcpy(message->fs_name, &NF2FS_FS_NAME, strlen(NF2FS_FS_NAME)) ||
                    NF2FS_VERSION != message->version ||
                    NF2FS->cfg->sector_size != message->sector_size ||
                    NF2FS->cfg->sector_count != message->sector_count ||
                    NF2FS->cfg->name_max != message->name_max ||
                    NF2FS->cfg->file_max != message->file_max ||
                    NF2FS->cfg->region_cnt != message->region_cnt) {
                    err = NF2FS_ERR_WRONGCFG;
                    goto cleanup;
                }
                break;
            }

            case NF2FS_DATA_ID_MAP: {
                // update id map message
                NF2FS_mapaddr_flash_t *flash_map = (NF2FS_mapaddr_flash_t *)data;
                err = NF2FS_idmap_assign(NF2FS, NF2FS->id_map, flash_map);
                if (err)
                    goto cleanup;
                break;
            }

            case NF2FS_DATA_SECTOR_MAP: {
                // update sector map message
                NF2FS_mapaddr_flash_t* flash_map= (NF2FS_mapaddr_flash_t*)data;
                NF2FS_size_t num= NF2FS_alignup(NF2FS->cfg->sector_count * 2 / 8, NF2FS->cfg->sector_size) /
                                 NF2FS->cfg->sector_size;
                err= NF2FS_smap_assign(NF2FS, NF2FS->manager, flash_map, num);
                if (err)
                    goto cleanup;
                break;
            }

            case NF2FS_DATA_WL_ADDR: {
                // update WL message
                err= NF2FS_wl_init(NF2FS, &NF2FS->manager->wl);
                if (err)
                    return err;
                NF2FS_wladdr_flash_t *wladdr = (NF2FS_wladdr_flash_t *)data;
                NF2FS->manager->wl->begin= wladdr->begin;
                NF2FS->manager->wl->off= wladdr->off;
                NF2FS->manager->wl->etimes= wladdr->erase_times;
                // TODO in the future, should update other wl message
                break;
            }

            case NF2FS_DATA_DIR_NAME:
            case NF2FS_DATA_NDIR_NAME: {
                // update root dir tree entry
                NF2FS_ASSERT(NF2FS_dhead_id(head) == NF2FS_ID_ROOT);
                NF2FS_dir_name_flash_t* name= (NF2FS_dir_name_flash_t*)data;
                err= NF2FS_tree_entry_add(NF2FS->ram_tree, NF2FS_ID_SUPER, NF2FS_ID_ROOT, NF2FS->superblock->sector,
                                         NF2FS->superblock->free_off, name->tail, (char *)name->name, 0);
                if (err)
                    goto cleanup;

                // add open root dir to dir list
                NF2FS_dir_ram_t *root_dir= NULL;
                err= NF2FS_dir_lowopen(NF2FS, name->tail, NF2FS_ID_ROOT, NF2FS_ID_SUPER, NF2FS->superblock->sector,
                                      NF2FS->superblock->free_off, &root_dir, NF2FS->pcache);
                if (err)
                    goto cleanup;
                break;
            }

            case NF2FS_DATA_REGION_MAP: {
                // region map message, size is 20B
                NF2FS_region_map_flash_t* region_map= (NF2FS_region_map_flash_t*)data;
                NF2FS_region_map_assign(NF2FS, NF2FS->manager->region_map, region_map, NF2FS->superblock->sector,
                                       NF2FS->superblock->free_off);
                break;
            }

            case NF2FS_DATA_COMMIT: {
                // update with commit message
                NF2FS_commit_flash_t* commit= (NF2FS_commit_flash_t*)data;
                err= NF2FS_init_with_commit(NF2FS, commit, NF2FS->pcache);
                if (err)
                    goto cleanup;

                // set the commit message to delete, if do not have it, corrupt happens
                NF2FS_head_validate(NF2FS, NF2FS->superblock->sector,
                                   NF2FS->superblock->free_off, NF2FS_DHEAD_DELETE_SET);

                NF2FS->superblock->free_off+= len;
                return err;
            }

            case NF2FS_DATA_DELETE:
                // Just skip, no need to do anything.
                break;

            case NF2FS_DATA_FREE:
                // fail to find valid commit message, corrupt happens
                err= NF2FS_ERR_CORRUPT;
                goto cleanup;

            default:
                // Other type of data head shouldn't be read, so there has been some problem.
                err = NF2FS_ERR_CORRUPT;
                goto cleanup;
            }

            NF2FS->superblock->free_off += len;
            data += len;

            // the next data head is not entire, read again
            if (NF2FS->superblock->free_off - NF2FS->rcache->off + sizeof(NF2FS_head_t) >= NF2FS->cfg->cache_size)
                break;
        }
    }

cleanup:
    NF2FS_deinit(NF2FS);
    return err;
}

// unmount NF2FS
int NF2FS_unmount(NF2FS_t *NF2FS)
{
    int err = NF2FS_ERR_OK;

    // Flush files
    NF2FS_file_ram_t *file = NF2FS->file_list;
    while (file != NULL) {
        err = NF2FS_file_flush(NF2FS, file);
        if (err)
            return err;
        file = file->next_file;
    }

    // flush dir
    NF2FS_dir_ram_t *dir = NF2FS->dir_list;
    while (dir != NULL) {
        // record old space in dir
        NF2FS_dir_ospace_flash_t old_space= {
            .head= NF2FS_MKDHEAD(0, 1, dir->id, NF2FS_DATA_DIR_OSPACE, sizeof(NF2FS_dir_ospace_flash_t)),
            .old_space= dir->old_space,
        };

        err= NF2FS_dir_prog(NF2FS, dir, &old_space, sizeof(NF2FS_dir_ospace_flash_t));
        if (err)
            return err;
        dir = dir->next_dir;
    }

    // Flush region map to flash.
    err = NF2FS_region_map_flush(NF2FS, NF2FS->manager->region_map);
    if (err)
        return err;

    // Prog new commit message.
    NF2FS_commit_flash_t commit= {
        .head= NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_COMMIT, sizeof(NF2FS_commit_flash_t)),
        .next_id= NF2FS->id_map->free_map->region * NF2FS->id_map->ids_in_buffer + NF2FS->id_map->free_map->index_or_changed,
        .scan_times= NF2FS->manager->scan_times,
        .next_dir_sector= NF2FS->manager->dir_map->region * NF2FS->manager->region_size + NF2FS->manager->dir_map->index_or_changed,
        .next_bfile_sector= NF2FS->manager->bfile_map->region * NF2FS->manager->region_size + NF2FS->manager->bfile_map->index_or_changed,
        .reserve_region= NF2FS->manager->region_map->reserve,
    };

    err= NF2FS_prog_in_superblock(NF2FS, NF2FS->superblock, &commit, sizeof(NF2FS_commit_flash_t));
    if (err)
        return err;

    // Flush free dir map to flash.
    err= NF2FS_smap_flush(NF2FS, NF2FS->manager);
    if (err)
        return err;

    // Flush data in pcache to flash.
    err = NF2FS_cache_flush(NF2FS, NF2FS->pcache);
    if (err)
        return err;

    // Free all in-ram structre's memory.
    NF2FS_deinit(NF2FS);
    return err;
}

/**
 * -------------------------------------------------------------------------------------------------------
 * -------------------------------------    File level operations    -------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// open a file
int NF2FS_file_open(NF2FS_t* NF2FS, NF2FS_file_ram_t** file, char* path, int flags)
{
    int err = NF2FS_ERR_OK;

    // check whether open file is too much
    NF2FS_file_ram_t* temp_file= NF2FS->file_list;
    NF2FS_size_t cnt= 0;
    while (temp_file != NULL) {
        cnt++;
        temp_file= temp_file->next_file;
    }
    if (cnt >= NF2FS_FILE_LIST_MAX)
        return NF2FS_ERR_MUCHOPEN;

    // find the father dir
    NF2FS_tree_entry_ram_t *entry = NULL;
    err= NF2FS_father_dir_find(NF2FS, path, &entry);
    if (err)
        return err;
    NF2FS_ASSERT(entry->tail_sector != NF2FS_NULL);

    // Open father dir of file.
    NF2FS_dir_ram_t* father_dir= NULL;
    err= NF2FS_dir_lowopen(NF2FS, entry->tail_sector, entry->id, entry->father_id,
                          entry->name_sector, entry->name_off, &father_dir, NF2FS->rcache);
    if (err)
        return err;

    // Find The true name of file in path.
    char *name = NF2FS_name_in_path(path);
    if (*name == '/')
        name++;

    // Traverse dir to find whether or not the file is already in dir.
    NF2FS_tree_entry_ram_t temp_entry;
    err= NF2FS_dtraverse_name(NF2FS, father_dir->tail_sector, name, strlen(name),
                             NF2FS_DATA_REG, &temp_entry);
    if (err)
        return err;

    if (temp_entry.id != NF2FS_NULL) {
        // find the file name, open it
        err= NF2FS_file_lowopen(NF2FS, father_dir, temp_entry.id, temp_entry.name_sector,
                               temp_entry.name_off, strlen(name), file);
        if (err)
            return err;
    } else {
        // If not find, we should create a new file.
        err = NF2FS_create_file(NF2FS, father_dir, file, name, strlen(name));
        if (err)
            return err;
    }
    return err;
}

// close a file
int NF2FS_file_close(NF2FS_t* NF2FS, NF2FS_file_ram_t* file)
{
    int err = NF2FS_ERR_OK;
    
    // flush data to flash first
    err = NF2FS_file_flush(NF2FS, file);
    if (err)
        return err;

    // free the file
    err= NF2FS_file_free(&NF2FS->file_list, file);
    return err;
}

// read data of a file
int NF2FS_file_read(NF2FS_t* NF2FS, NF2FS_file_ram_t* file, void* buffer, NF2FS_size_t size)
{
    // error if read size is larger than file size
    if (file->file_pos + size > file->file_size) {
        NF2FS_ERROR("file message wrong before reading\r\n");
        return NF2FS_ERR_INVAL;
    }

    if (file->file_size <= NF2FS_FILE_SIZE_THRESHOLD) {
        // Small file read function.
        return NF2FS_small_file_read(NF2FS, file, buffer, size);
    } else {
        // big file read function
        return NF2FS_big_file_read(NF2FS, file, buffer, size);
    }
}

// write data to a file
int NF2FS_file_write(NF2FS_t* NF2FS, NF2FS_file_ram_t* file, void* buffer, NF2FS_size_t size)
{
    // Error if file size is larger than max size after writing
    if (file->file_pos + size > NF2FS->cfg->file_max) {
        return NF2FS_ERR_FBIG;
    }

    if (file->file_size <= NF2FS_FILE_SIZE_THRESHOLD &&
        file->file_pos + size <= NF2FS_FILE_SIZE_THRESHOLD) {
        // prog small file
        return NF2FS_small_file_write(NF2FS, file, buffer, size);
    } else if (file->file_size >= 0 &&
               file->file_size <= NF2FS_FILE_SIZE_THRESHOLD &&
               file->file_size + size > NF2FS_FILE_SIZE_THRESHOLD) {
        // change small file to big file
        return NF2FS_s2b_file_write(NF2FS, file, buffer, size);
    } else if (file->file_size + size > NF2FS_FILE_SIZE_THRESHOLD) {
        // prog data to a existing big file
        return NF2FS_big_file_write(NF2FS, file, buffer, size);
    }
    return NF2FS_ERR_INVAL;
}

// change the file position
int NF2FS_file_seek(NF2FS_t* NF2FS, NF2FS_file_ram_t* file, NF2FS_soff_t off, int whence)
{
    switch (whence)
    {
    case NF2FS_SEEK_SET:
        // Absolute position
        if (off < 0 || off > file->file_size)
            return NF2FS_ERR_INVAL;
        file->file_pos = off;
        break;

    case NF2FS_SEEK_CUR:
        // Current + off;
        if ((NF2FS_soff_t)file->file_pos + off < 0 ||
            (NF2FS_soff_t)file->file_pos + off > file->file_size)
            return NF2FS_ERR_INVAL;
        else
            file->file_pos = file->file_pos + off;
        break;

    case NF2FS_SEEK_END: {
        // end position + off
        NF2FS_soff_t res = file->file_size + off;
        if (res < 0 || res > file->file_size)
            return NF2FS_ERR_INVAL;
        else
            file->file_pos = res;
        break;
    }

    default:
        return NF2FS_ERR_INVAL;
    }
    return NF2FS_ERR_OK;
}

// delete a file
int NF2FS_file_delete(NF2FS_t* NF2FS, NF2FS_file_ram_t* file)
{
    int err= NF2FS_ERR_OK;

    // delete sectors belong to big file
    NF2FS_head_t head= *(NF2FS_head_t*)file->file_cache.buffer;
    NF2FS_ASSERT(head != NF2FS_NULL);
    if (NF2FS_dhead_type(head) == NF2FS_DATA_BFILE_INDEX) {
        // Because head in buffer may be old, we use size in file cache.
        NF2FS_bfile_index_flash_t *index = (NF2FS_bfile_index_flash_t *)file->file_cache.buffer;
        NF2FS_size_t num = (file->file_cache.size - sizeof(NF2FS_head_t)) / sizeof(NF2FS_bfile_index_ram_t);
        err = NF2FS_bfile_sector_old(NF2FS, &index->index[0], num);
        if (err)
            return err;
    }

    // Delete small file's data or big file's index.
    err = NF2FS_data_delete(NF2FS, file->father_id, file->file_cache.sector,
                             file->file_cache.off, NF2FS_dhead_dsize(head));
    if (err)
        return err;

    // Delete file's name in its father dir.
    err= NF2FS_data_delete(NF2FS, file->father_id, file->sector,
                          file->off, file->namelen + sizeof(NF2FS_file_name_flash_t));
    if (err)
        return err;

    // Free id that the file belongs to.
    err = NF2FS_id_free(NF2FS, NF2FS->id_map, file->id);
    if (err)
        return err;

    // Free in-ram memory of the file.
    err = NF2FS_file_free(&NF2FS->file_list, file);
    return err;
}

// flush file data to flash
int NF2FS_file_sync(NF2FS_t* NF2FS, NF2FS_file_ram_t* file)
{
    return NF2FS_file_flush(NF2FS, file);
}

/**
 * -------------------------------------------------------------------------------------------------------
 * -------------------------------------    Dir level operations    --------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// open a dir
int NF2FS_dir_open(NF2FS_t* NF2FS, NF2FS_dir_ram_t** dir, char* path)
{
    int err = NF2FS_ERR_OK;

    // fail to open if opened dir is too much
    NF2FS_dir_ram_t* temp_dir= NF2FS->dir_list;
    NF2FS_size_t cnt= 0;
    while (temp_dir != NULL) {
        cnt++;
        temp_dir= temp_dir->next_dir;
    }
    if (cnt >= NF2FS_DIR_LIST_MAX)
        return NF2FS_ERR_MUCHOPEN;

    // find father dir of the open dir
    NF2FS_tree_entry_ram_t *father_entry = NULL;
    err= NF2FS_father_dir_find(NF2FS, path, &father_entry);
    if (err)
        return err;

    // Open father dir of dir.
    NF2FS_dir_ram_t* father_dir= NULL;
    err= NF2FS_dir_lowopen(NF2FS, father_entry->tail_sector, father_entry->id, father_entry->father_id,
                          father_entry->name_sector, father_entry->name_off, &father_dir, NF2FS->rcache);
    if (err)
        return err;

    // Find The true name of file in path.
    char *name = NF2FS_name_in_path(path);
    if (*name == '/')
        name++;

    // find opened dir in ram-tree
    NF2FS_size_t tree_index= NF2FS_NULL;
    NF2FS_tree_entry_ram_t temp_entry;
    err= NF2FS_tree_entry_name_find(NF2FS, name, strlen(name), father_dir->id, &tree_index);
    if (err) {
        // find opened dir in flash
        err= NF2FS_dtraverse_name(NF2FS, father_dir->tail_sector, name, strlen(name),
                                NF2FS_DATA_REG, &temp_entry);
        if (err)
            return err;
    }

    if (tree_index == NF2FS_NULL && temp_entry.id == NF2FS_NULL) {
        // fail to find opened dir, create one
        err = NF2FS_create_dir(NF2FS, father_dir, dir, name, strlen(name));
        if (err)
            return err;
    } else {
        // find tree entry of the opened dir
        if (tree_index != NF2FS_NULL) {
            memcpy(&temp_entry, &NF2FS->ram_tree->tree_array[tree_index], sizeof(NF2FS_tree_entry_ram_t));
        }
        NF2FS_ASSERT(temp_entry.id != NF2FS_NULL);

        // open the dir
        err= NF2FS_dir_lowopen(NF2FS, temp_entry.tail_sector, temp_entry.id, temp_entry.father_id,
                                  temp_entry.name_sector, temp_entry.name_off, dir, NF2FS->rcache);
        if (err)
            return err;
    }
    return err;
}

// close a dir
int NF2FS_dir_close(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir)
{
    int err = NF2FS_ERR_OK;

    // if file still opens, can not close dir.
    NF2FS_file_ram_t* file= NF2FS->file_list;
    while (file != NULL) {
        if (file->father_id == dir->id) {
            NF2FS_ERROR("files are still opened, can not close dir!!\n");
            return NF2FS_ERR_WRONGPROG;
        }
        file= file->next_file;
    }

    // record old space in dir
    NF2FS_dir_ospace_flash_t old_space= {
        .head= NF2FS_MKDHEAD(0, 1, dir->id, NF2FS_DATA_DIR_OSPACE, sizeof(NF2FS_dir_ospace_flash_t)),
        .old_space= dir->old_space,
    };
    err= NF2FS_dir_prog(NF2FS, dir, &old_space, sizeof(NF2FS_dir_ospace_flash_t));
    if (err)
        return err;

    // flush cache data to flash
    err = NF2FS_cache_flush(NF2FS, NF2FS->pcache);
    if (err)
        return err;

    // Delete it in dir list.
    NF2FS_dir_ram_t *pre_dir = NF2FS->dir_list;
    if (pre_dir->id == dir->id) {
        NF2FS->dir_list = pre_dir->next_dir;
    } else {
        while (pre_dir->next_dir != NULL) {
            if (pre_dir->next_dir->id == dir->id)
                break;
            pre_dir = pre_dir->next_dir;
        }
        if (pre_dir == NULL)
            return NF2FS_ERR_NODIROPEN;
        pre_dir->next_dir = dir->next_dir;
    }

    // free dir's memory
    NF2FS_free(dir);
    return err;
}

// delete a dir
int NF2FS_dir_delete(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir)
{
    int err = NF2FS_ERR_OK;

    // Find dir's father dir
    NF2FS_dir_ram_t* father_dir= NULL;
    err= NF2FS_open_dir_find(NF2FS, dir->father_id, &father_dir);
    if (err)
        return err;

    // delete all sectors that belong to dir's son file
    err = NF2FS_dtraverse_bfile_delete(NF2FS, dir);
    if (err)
        return err;

    // Set all sectors belongs to dir to old.
    err = NF2FS_dir_old(NF2FS, dir->tail_sector);
    if (err)
        return err;

    // Delete dir name entry in father dir.
    err = NF2FS_data_delete(NF2FS, father_dir->id, dir->name_sector, dir->name_off,
                             sizeof(NF2FS_dir_name_flash_t) + dir->namelen);
    if (err)
        return err;

    // Delete the tree entry
    err= NF2FS_tree_entry_remove(NF2FS->ram_tree, dir->id);
    if (err)
        return err;

    // Free id that belongs to dir.
    err = NF2FS_id_free(NF2FS, NF2FS->id_map, dir->id);
    if (err)
        return err;

    // Free in-ram dir structure.
    err = NF2FS_dir_free(NF2FS->dir_list, dir);
    return err;
}

// read an dir entry from dir.
int NF2FS_dir_read(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir, NF2FS_info_ram_t* info)
{
    int err = NF2FS_ERR_OK;
    memset(info, 0, sizeof(*info));

    // If it's not initialized, we should do it.
    if (dir->pos_sector == NF2FS_NULL) {
        dir->pos_sector = dir->tail_sector;
        dir->pos_off = 0;
        dir->pos_presector = NF2FS_NULL;
    }

    uint8_t *data = NULL;
    NF2FS_size_t head;
    NF2FS_size_t len = sizeof(NF2FS_head_t);
    while (true) {
        if (~((NF2FS->rcache->sector == dir->pos_sector) &&
              (NF2FS->rcache->off + NF2FS->rcache->size >= dir->pos_off + len) &&
              (NF2FS->rcache->off <= dir->pos_off))) {
            
            // read other data to cache
            err = NF2FS_read_to_cache(NF2FS, NF2FS->rcache, dir->pos_sector, dir->pos_off,
                                       NF2FS_min(NF2FS->cfg->cache_size, NF2FS->cfg->sector_size - dir->pos_off));
            if (err)
                return err;

            data = NF2FS->rcache->buffer;
        } else {
            // get the next head position
            data = NF2FS->rcache->buffer + dir->pos_off - NF2FS->rcache->off;
        }

        if (dir->pos_off == 0) {
            // record sector message for traversing
            head = *(NF2FS_head_t *)data;
            err = NF2FS_shead_check(head, NF2FS_STATE_USING, NF2FS_SECTOR_DIR);
            if (err)
                return err;

            NF2FS_dir_sector_flash_t *dir_sector = (NF2FS_dir_sector_flash_t *)data;
            dir->pos_presector = dir_sector->pre_sector;
            data += sizeof(NF2FS_dir_sector_flash_t);
            dir->pos_off += sizeof(NF2FS_dir_sector_flash_t);
            len = sizeof(NF2FS_head_t);
        }

        bool if_next = false;
        while (true) {
            head= *(NF2FS_head_t*)data;
            
            // Check if the head is valid.
            err = NF2FS_dhead_check(head, NF2FS_NULL, (int)NF2FS_NULL);
            if (err) {
                return err;
            }

            len = NF2FS_dhead_dsize(head);
            if (dir->pos_off + len > NF2FS->rcache->off + NF2FS->rcache->size) {
                if (head == NF2FS_NULL && dir->pos_presector != NF2FS_NULL) {
                    // time to traverse next sector
                    dir->pos_sector = dir->pos_presector;
                    dir->pos_off = 0;
                    len = sizeof(NF2FS_head_t);
                    break;
                } else if (head == NF2FS_NULL && dir->pos_presector == NF2FS_NULL) {
                    // finishing traversing
                    return err;
                }
                // reread data to cache
                break;
            }

            switch (NF2FS_dhead_type(head))
            {
            case NF2FS_DATA_NDIR_NAME:
            case NF2FS_DATA_DIR_NAME: {
                NF2FS_dir_name_flash_t *dname = (NF2FS_dir_name_flash_t *)data;
                info->type = NF2FS_DATA_DIR;
                memcpy(info->name, dname->name, len - sizeof(NF2FS_dir_name_flash_t));
                info->name[len - sizeof(NF2FS_dir_name_flash_t)] = '\0';
                dir->pos_off += len;
                return err;
            }

            case NF2FS_DATA_NFILE_NAME:
            case NF2FS_DATA_FILE_NAME: {
                NF2FS_file_name_flash_t *fname = (NF2FS_file_name_flash_t *)data;
                info->type = NF2FS_DATA_REG;
                memcpy(info->name, fname->name, len - sizeof(NF2FS_file_name_flash_t));
                info->name[len - sizeof(NF2FS_file_name_flash_t)] = '\0';
                dir->pos_off += len;
                return err;
            }

            case NF2FS_DATA_DELETE:
            case NF2FS_DATA_BFILE_INDEX:
            case NF2FS_DATA_SFILE_DATA:
            case NF2FS_DATA_DIR_OSPACE:    
                break;

            case NF2FS_DATA_FREE:
                NF2FS_ASSERT(head == NF2FS_NULL);
                if_next = true;
                break;

            default:
                break;
            }

            if (if_next) {
                if (dir->pos_presector == NF2FS_NULL) {
                    // the end of readdir
                    info->type= NF2FS_DATA_DELETE;
                    return err;
                }
                dir->pos_sector = dir->pos_presector;
                dir->pos_off = 0;
                break;
            } else {
                dir->pos_off += len;
                data += len;
                len= sizeof(NF2FS_head_t);
                if (dir->pos_off + sizeof(NF2FS_head_t) < NF2FS->rcache->off + NF2FS->rcache->size)
                    break;
            }
        }
    }
}
