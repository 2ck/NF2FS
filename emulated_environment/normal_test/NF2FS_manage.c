/**
 * The basic manager operations of NF2FS
 */

#include "NF2FS.h"
#include "NF2FS_manage.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NF2FS_rw.h"
#include "NF2FS_head.h"
#include "NF2FS_util.h"

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------    Basic region operations    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// free region map
void NF2FS_region_map_free(NF2FS_region_map_ram_t* region_map)
{
    if (region_map) {
        if (region_map->dir_region)
            NF2FS_free(region_map->dir_region);
        if (region_map->bfile_region)
            NF2FS_free(region_map->bfile_region);
        NF2FS_free(region_map);
    }
}

// init function of region map.
int NF2FS_region_map_init(NF2FS_size_t region_num, NF2FS_region_map_ram_t** region_map_addr)
{
    int err = NF2FS_ERR_OK;
    NF2FS_size_t size;

    // Allocate memory for region map.
    NF2FS_region_map_ram_t *region_map = NF2FS_malloc(sizeof(NF2FS_region_map_ram_t));
    if (!region_map)
    {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // the size allocated for region map buffer
    size= NF2FS_alignup(region_num, sizeof(uint32_t) * 8) / 8;

    // Init the dir region
    region_map->dir_region = NF2FS_malloc(size);
    if (!region_map->dir_region) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }
    memset(region_map->dir_region, 0xff, size);

    // Init the big file region
    region_map->bfile_region = NF2FS_malloc(size);
    if (!region_map->bfile_region) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }
    memset(region_map->dir_region, 0xff, size);

    *region_map_addr = region_map;
    return err;

cleanup:
    NF2FS_region_map_free(region_map);
    return err;
}

// Assign message for region map when mount, format
void NF2FS_region_map_assign(NF2FS_t *NF2FS, NF2FS_region_map_ram_t *ram_region,
                            NF2FS_region_map_flash_t *flash_region,
                            NF2FS_size_t sector, NF2FS_size_t free_off)
{
    ram_region->begin = sector;
    ram_region->off= free_off;
    ram_region->change_flag= 0;

    NF2FS_size_t len= NF2FS->cfg->region_cnt / 8;
    memcpy(ram_region->dir_region, flash_region->map, len);
    memcpy(ram_region->bfile_region, flash_region->map + len, len);
}

// The region is used for dir or big file
int NF2FS_region_type(NF2FS_region_map_ram_t *region_map, NF2FS_size_t region)
{
    if (region == region_map->reserve) {
        NF2FS_ERROR("WRONG region type\n");
        return NF2FS_SECTOR_RESERVE;
    }

    int i = region / (sizeof(uint32_t) * 8);
    int j = region % (sizeof(uint32_t) * 8);
    if ((region_map->dir_region[i] >> j) & 1U) {
        return NF2FS_SECTOR_DIR;
    } else if ((region_map->bfile_region[i] >> j) & 1U) {
        return NF2FS_SECTOR_BFILE;
    } else {
        NF2FS_ERROR("WRONG region type\n");
        return NF2FS_ERR_INVAL;
    }
}

// flush region map to NOR flash.
int NF2FS_region_map_flush(NF2FS_t* NF2FS, NF2FS_region_map_ram_t* region_map)
{
    int err= NF2FS_ERR_OK;
    NF2FS_size_t map_len = NF2FS->manager->region_num / 8;

    if (region_map->change_flag == NF2FS_REGION_MAP_NOCHANGE) {
        // If nothing changed, we should not flush
        return err;
    } else if (region_map->change_flag == NF2FS_REGION_MAP_IN_PLACE_CHANGE) {
        // If no need to alloc a new region map
        // update dir region message
        NF2FS_size_t temp_off= region_map->off + sizeof(NF2FS_head_t);
        err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_DATA, region_map->begin,
                              temp_off, map_len, region_map->dir_region);
        if (err)
            return err;

        // update big file region message
        temp_off+= map_len;
        err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_DATA, region_map->begin,
                              temp_off, map_len, region_map->bfile_region);
        return err;
    }

    // Create in-flash region map structure.
    // should be freed after using.
    NF2FS_size_t len = sizeof(NF2FS_region_map_flash_t) + map_len;
    NF2FS_region_map_flash_t *flash_map = NF2FS_malloc(len);
    if (!flash_map) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Assign data for in-flash region map.
    flash_map->head = NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_REGION_MAP, len);
    memcpy((uint8_t *)flash_map->map, (uint8_t *)region_map->dir_region, map_len);

    // Prog to NOR flash directly
    err= NF2FS_prog_in_superblock(NF2FS, NF2FS->superblock, flash_map, len);
    
cleanup:
    if (flash_map)
        NF2FS_free(flash_map);
    return err;
}

// Return the correct region map according to the sector map type
uint32_t* NF2FS_region_map_get(NF2FS_flash_manage_ram_t* manager, int smap_type, NF2FS_off_t** region_index)
{
    switch (smap_type) {
    case NF2FS_SECTOR_BFILE:
        *region_index= &manager->region_map->bfile_index;
        return manager->region_map->bfile_region;
    case NF2FS_SECTOR_DIR:
        *region_index= &manager->region_map->dir_index;
        return manager->region_map->dir_region;
    case NF2FS_SECTOR_META:
    case NF2FS_SECTOR_RESERVE:
    default:
        NF2FS_ERROR("Wrong smap type!\n");
    }
    return NULL;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Basic map operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Init sector, id map
int NF2FS_map_init(NF2FS_t *NF2FS, NF2FS_map_ram_t **map_addr, NF2FS_size_t buffer_len)
{
    int err = NF2FS_ERR_OK;

    // Allocate memory for map.
    NF2FS_map_ram_t *map = NF2FS_malloc(sizeof(NF2FS_map_ram_t) + buffer_len);
    if (!map)
    {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Initialize basic message.
    memset(map, 0xff, sizeof(NF2FS_map_ram_t) + buffer_len);
    map->index_or_changed= 0;
    *map_addr = map;
    return err;

cleanup:
    if (map)
        NF2FS_free(map);
    return err;
}

// Flush in-ram sector, id map into flash.
int NF2FS_map_flush(NF2FS_t *NF2FS, NF2FS_map_ram_t *map, NF2FS_size_t buffer_len,
                   NF2FS_size_t map_begin, NF2FS_size_t map_off)
{
    int err = NF2FS_ERR_OK;

    // if map has not stored data, return directly
    if (map->region == NF2FS_NULL)
        return err;

    // find the position to flush
    NF2FS_size_t sector = map_begin;
    NF2FS_size_t off = map_off + map->region * buffer_len;
    while (off >= NF2FS->cfg->sector_size)
    {
        sector++;
        off -= NF2FS->cfg->sector_size;
    }

    // Flush to NOR flash
    // without the head structure, we use cfg->prog directly.
    err = NF2FS->cfg->prog(NF2FS->cfg, sector, off, map->buffer, buffer_len);
    NF2FS_ASSERT(err <= 0);
    return err;
}

// flush the erase map and reset for another region
int NF2FS_erase_map_flush(NF2FS_t* NF2FS, NF2FS_map_ram_t* emap, NF2FS_size_t next_region)
{
    int err= NF2FS_ERR_OK;

    // cal the address to prog in erase map
    NF2FS_size_t emap_begin= NF2FS->manager->smap_begin;
    NF2FS_size_t emap_off= NF2FS->manager->smap_off +
                        NF2FS_alignup(NF2FS->cfg->sector_count, 8) / 8;
    while (emap_off >= NF2FS->cfg->sector_size) {
        emap_begin++;
        emap_off-= NF2FS->cfg->sector_size;
    }

    // flush erase map to flash
    err= NF2FS_map_flush(NF2FS, emap, NF2FS->manager->region_size / 8,
                        emap_begin, emap_off);
    if (err)
        return err;

    // reset the map
    memset(emap->buffer, 0xff, NF2FS->manager->region_size / 8);
    emap->region = next_region;
    emap->index_or_changed = 0;
    emap->free_num= 0;
    return err;
}

// count the valid bits number (bit 1) in the buffer
int NF2FS_valid_bits_cnt(uint32_t* buffer, uint32_t bits_in_buffer)
{
    uint32_t cnt= 0;
    uint32_t i= 0;
    uint32_t j= 0;
    for (int loop= 0; loop < bits_in_buffer; loop++) {
        if ((buffer[i] >> j) & 1U)
            cnt++;

        j++;
        if (j == 32) {
            i++;
            j= 0;
        }
    }
    return cnt;
}

// The in-ram map change function.
int NF2FS_ram_map_change(NF2FS_t *NF2FS, NF2FS_size_t region, NF2FS_size_t bits_in_buffer,
                        NF2FS_map_ram_t *map, NF2FS_size_t map_begin, NF2FS_size_t map_off)
{
    int err= NF2FS_ERR_OK;

    // init basic message in map
    map->region = region;
    map->index_or_changed = 0;

    // cal the true position of data in flash
    NF2FS_size_t sector = map_begin;
    NF2FS_off_t off = map_off + map->region * bits_in_buffer / 8;
    while (off >= NF2FS->cfg->sector_size) {
        sector++;
        off-= NF2FS->cfg->sector_size;
    }

    // Read bitmap to buffer.
    err= NF2FS_direct_read(NF2FS, sector, off, bits_in_buffer / 8, map->buffer);
    if (err)
        return err;

    // count the valid bits in the map buffer
    map->free_num = NF2FS_valid_bits_cnt(map->buffer, bits_in_buffer);
    return err;
}

// Find the sequential num of sectors in map, return the first sector to begin.
int NF2FS_find_in_map(NF2FS_t *NF2FS, NF2FS_size_t bits_in_buffer, NF2FS_map_ram_t *map,
                     NF2FS_size_t num, NF2FS_size_t *begin)
{
    int cnt = 0;

    // If there are no enough free sectors, current alloc is failed.
    NF2FS_ASSERT(map->free_num != NF2FS_NULL);
    if (map->free_num < num)
        return 0;

    // There are 32 bits in NF2FS_size_t
    int uint32_bits = 32;
    int max = bits_in_buffer / uint32_bits;
    int i = map->index_or_changed / uint32_bits;
    int j = map->index_or_changed % uint32_bits;
    for (; i < max;) {
        for (; j < uint32_bits; j++) {
            // Find num sequential sectors to use.
            if ((map->buffer[i] >> j) & 1) {
                cnt++;
                if (cnt == num) {
                    j++;
                    break;
                }
            }
            else
                cnt = 0;
        }

        // find, return directly
        if (cnt == num)
            break;
        
        // find in the next loop
        j = 0;
        i++;
    }

    // There are two cases when we change bit map
    // 1. In the same NF2FS_size_t, i.e map->buffer[i]
    // 2. In map->buffer[i] and in map->buffer[i+1]
    if (cnt == num) {
        NF2FS_size_t origin = i * uint32_bits + j - num;
        NF2FS_size_t temp = 0;

        // If it's case 2, turn map->buffer[i] first.
        // TODO, need to change in the future.
        if (i > 0 && j < num) {
            for (int k = 0; k < num - j; k++)
                temp = (temp >> 1) | (1U << 31);
            map->buffer[0] &= ~temp;
            origin = uint32_bits * i;
        }

        // Turn others.
        temp = 0;
        for (int k = 0; k < i * uint32_bits + j - origin; k++) {
            temp = (temp << 1) | 1U;
        }
        map->buffer[i] &= ~(temp << ((j < num) ? 0 : j - num));

        // Change other things.
        map->free_num -= num;
        map->index_or_changed = i * uint32_bits + j;
        *begin = bits_in_buffer * map->region + map->index_or_changed - num;
        return cnt;
    }

    // tell the max number of sequential sectors in buffer
    if (cnt == 0)
        *begin = NF2FS_NULL;
    else
        *begin = bits_in_buffer * (map->region + 1) - cnt;
    return cnt;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Sector map operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Assign basic sector map message with sector addr data.
int NF2FS_smap_assign(NF2FS_t *NF2FS, NF2FS_flash_manage_ram_t *manager,
                            NF2FS_mapaddr_flash_t *map_addr, NF2FS_size_t num)
{
    int err= NF2FS_ERR_OK;

    manager->region_num= NF2FS->cfg->region_cnt;
    manager->region_size= NF2FS->cfg->sector_count / NF2FS->cfg->region_cnt;

    manager->smap_begin= map_addr->begin;
    manager->smap_off= map_addr->off;

    manager->etimes= NF2FS_malloc(num * sizeof(NF2FS_size_t));
    if (!manager->etimes)
        return NF2FS_ERR_NOMEM;
    for (int i = 0; i < num; i++)
        manager->etimes[i]= map_addr->erase_times[i];
    return err;
}

// Flush all sector map messages into flash.
// should only used in unmount or change the in-NOR map
int NF2FS_smap_flush(NF2FS_t *NF2FS, NF2FS_flash_manage_ram_t *manager)
{
    int err = NF2FS_ERR_OK;
    NF2FS_size_t len = manager->region_size / 8;

    // flush meta_map
    err = NF2FS_map_flush(NF2FS, manager->meta_map, len, manager->smap_begin,
                manager->smap_off);
    if (err)
        return err;

    // flush dir_map
    err = NF2FS_map_flush(NF2FS, manager->dir_map, len, manager->smap_begin,
                manager->smap_off);
    if (err)
        return err;
    
    // flush bfile_map
    err = NF2FS_map_flush(NF2FS, manager->bfile_map, len, manager->smap_begin,
                manager->smap_off);
    if (err)
        return err;
    
    // flush reserve_map
    err = NF2FS_map_flush(NF2FS, manager->reserve_map, len, manager->smap_begin,
                manager->smap_off);
    if (err)
        return err;

    // flush erase map if it records changes.
    if (manager->erase_map->index_or_changed) {
        NF2FS_size_t emap_begin= manager->smap_begin;
        NF2FS_size_t emap_off= manager->smap_off +
                              NF2FS_alignup(NF2FS->cfg->sector_count, 8) / 8;
        while (emap_off >= NF2FS->cfg->sector_size) {
            emap_begin++;
            emap_off-= NF2FS->cfg->sector_size;
        }
        err= NF2FS_map_flush(NF2FS, manager->erase_map,
                            len, emap_begin, emap_off);
    }
    return err;
}

// Change in-flash sector map.
int NF2FS_flash_smap_change(NF2FS_t *NF2FS, NF2FS_flash_manage_ram_t *manager,
                       NF2FS_cache_ram_t *pcache, NF2FS_cache_ram_t *rcache)
{
    int err= NF2FS_ERR_OK;

    err = NF2FS_smap_flush(NF2FS, manager);
    if (err)
        return err;

    err = NF2FS_cache_flush(NF2FS, pcache);
    if (err)
        return err;

    // find a new place for in-flash bitmap
    NF2FS_size_t old_sector = manager->smap_begin;
    NF2FS_size_t old_off = manager->smap_off;
    NF2FS_size_t need_space = 2 * NF2FS->cfg->sector_count / 8;
    NF2FS_size_t num = NF2FS_alignup(need_space, NF2FS->cfg->sector_size) /
                        NF2FS->cfg->sector_size;
    if (old_off + need_space >= NF2FS->cfg->sector_size) {
        // We need to find new sector to store map message.
        NF2FS_size_t new_begin = NF2FS_NULL;
        err = NF2FS_sector_alloc(NF2FS, manager, NF2FS_SECTOR_MAP, num, NF2FS_NULL,
                                  NF2FS_NULL, NF2FS_NULL, &new_begin, NULL);
        if (err)
            return err;

        // erase old sector map
        err= NF2FS_map_sector_erase(NF2FS, manager->smap_begin, num, manager->etimes);
        if (err)
            return err;

        // erase the newly allocated sectors.
        // do not need to write a new sector head because they store maps
        for (int i= 0; i < num; i++) {
            NF2FS_size_t head;
            bool if_erase= NF2FS_sector_erase(NF2FS, new_begin + num, &head);

            // cal the erase times
            if (head == NF2FS_NULL) {
                manager->etimes[i]= 0;
            } else if (if_erase) {
                manager->etimes[i]= NF2FS_dhead_dsize(head) + 1;
            } else {
                manager->etimes[i]= NF2FS_dhead_dsize(head);
            }
        }

        // Update basic map message.
        manager->smap_begin= new_begin;
        manager->smap_off= 0;
    } else {
        // If we do not need new sectors
        manager->smap_off+= need_space;
    }

    // Read the free map and remove map to cache
    need_space /= 2;
    NF2FS_size_t new_sector= manager->smap_begin;
    NF2FS_off_t off = manager->smap_off;
    NF2FS_size_t old_sector2 = old_sector + num / 2;
    NF2FS_off_t old_off2= (old_off + need_space >= NF2FS->cfg->sector_size) ? 0 : old_off + need_space;
    while (need_space > 0) {
        NF2FS_size_t size= NF2FS_min(NF2FS->cfg->sector_size - off,
                                   NF2FS_min(need_space, NF2FS->cfg->cache_size));

        // Read free map to pcache
        err = NF2FS_read_to_cache(NF2FS, pcache, old_sector, old_off, size);
        if (err)
            return err;

        // Read remove map to rcache
        err = NF2FS_read_to_cache(NF2FS, rcache, old_sector2, old_off2, size);
        if (err)
            return err;

        // Emerge into one free map.
        NF2FS_size_t *data1 = (NF2FS_size_t *)pcache->buffer;
        NF2FS_size_t *data2 = (NF2FS_size_t *)rcache->buffer;
        for (int i = 0; i < size; i += sizeof(NF2FS_size_t)) {
            // XNOR operation
            *data1 = ~(*data1 ^ *data2);
            data1++;
            data2++;
        }

        // Program the new free map into flash, we use cfg->prog with the head structure
        err = NF2FS->cfg->prog(NF2FS->cfg, new_sector, off, pcache->buffer, size);
        NF2FS_ASSERT(err <= 0);
        if (err) {
            return err;
        }

        off += size;
        old_off += size;
        old_off2 += size;
        if (old_off >= NF2FS->cfg->sector_size) {
            off = 0;
            old_off = 0;
            old_off2 = 0;
            new_sector++;
            old_sector++;
            old_sector2++;
        }
        need_space -= size;
    }

    NF2FS_cache_one(NF2FS, pcache);

    // prog the new map_addr to superblock
    NF2FS_size_t len = sizeof(NF2FS_mapaddr_flash_t) + num * sizeof(NF2FS_size_t);
    NF2FS_mapaddr_flash_t *addr = NF2FS_malloc(len);
    if (addr == NULL) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // fill message
    addr->head = NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_SECTOR_MAP, len);
    addr->begin = manager->smap_begin;
    addr->off = manager->smap_off;
    for (int i = 0; i < num; i++) {
        addr->erase_times[i] = manager->etimes[i];
    }
    err= NF2FS_prog_in_superblock(NF2FS, NF2FS->superblock, &addr, len);

    // we have scanned nor flash one time, increase it.
    manager->scan_times++;

cleanup:
    // we should finally free the allocated message.
    if (addr != NULL)
        NF2FS_free(addr);
    return err;
}

// Return the correct map according to the sector map type
NF2FS_map_ram_t* NF2FS_smap_get(NF2FS_flash_manage_ram_t* manager, int smap_type)
{
    switch (smap_type) {
    case NF2FS_SECTOR_BFILE:
        return manager->bfile_map;
    case NF2FS_SECTOR_DIR:
        return manager->dir_map;
    case NF2FS_SECTOR_META:
        return manager->meta_map;
    case NF2FS_SECTOR_RESERVE:
        return manager->reserve_map;
    default:
        NF2FS_ERROR("Wrong smap type!\n");
        break;
    }
    return NULL;
}

// Find next region of sector map to scan.
// not including the erase, meta, and reserve map.
int NF2FS_sector_nextsmap(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t* manager, int smap_type)
{
    int err= NF2FS_ERR_OK;

    // these two map has fixed region
    NF2FS_map_ram_t* map= NF2FS_smap_get(manager, smap_type);
    if (smap_type == NF2FS_SECTOR_META || smap_type == NF2FS_SECTOR_RESERVE) {
        map->free_num= NF2FS_valid_bits_cnt(map->buffer, manager->region_size);
        map->index_or_changed= 0;
        // if it's format, we alloc region 0 as meta region
        if (map->region == NF2FS_NULL) {
            NF2FS_ASSERT(manager->region_map->reserve == 0);
            map->region= manager->region_map->reserve;
            manager->region_map->reserve++;
        }
        return err;
    }

    // Get pointers, the pointers should not be NULL
    NF2FS_size_t* region_index;
    uint32_t* region_buffer= NF2FS_region_map_get(manager, smap_type, &region_index);
    if (region_buffer == NULL || map == NULL)
        return NF2FS_ERR_INVAL;

    // Flushing valid data to nor flash first.
    if (map->region != NF2FS_NULL) {
        err = NF2FS_map_flush(NF2FS, map, manager->region_size / 8, manager->smap_begin, manager->smap_off);
        if (err)
            return err;
    }

    // Change to the next region for dir, bfile map
    int uint32_bits= 32;
    if (manager->scan_times < NF2FS_WL_START) {
        // If we first scan NOR flash, reserve is used to indicate the next free region
        if (manager->scan_times == 0 && manager->region_map->reserve != manager->region_num - 1) {
            // use reserve region as the next free region
            map->region= manager->region_map->reserve;
            map->free_num= manager->region_size;
            map->index_or_changed= 0;
            memset(map->buffer, 0xff, manager->region_size / 8);

            // update region map message
            region_buffer[manager->region_map->reserve / uint32_bits]&=
                ~(1U << (manager->region_map->reserve % uint32_bits));
            *region_index= manager->region_map->reserve + 1;
            manager->region_map->change_flag= NF2FS_REGION_MAP_IN_PLACE_CHANGE;
            manager->region_map->reserve++;
            return err;
        }

        // change buffered map in the normal scan without WL
        NF2FS_size_t i = *region_index / uint32_bits;
        NF2FS_size_t j = *region_index % uint32_bits;
        while (true) {
            // find all regions, but don's have another one.
            if (*region_index == map->region) {
                // TODO in the future
                // steal from the other region map, or there really has no more space.
                err = NF2FS_ERR_NOSPC;
                return err;
            }

            // Looped to find the next used region.
            // In nor flash, bit 0 means used, bit 1 means not used.
            if (~((region_buffer[i] >> j) & 1U)) {
                err= NF2FS_ram_map_change(NF2FS, *region_index, manager->region_size, map,
                                         manager->smap_begin, manager->smap_off);
                *region_index = *region_index + 1;
                return err;
            }

            // Update basic message for next loop
            *region_index = *region_index + 1;
            j++;
            if (j == uint32_bits) {
                i++;
                j = 0;
            }

            // change the in-flash map if we scan flash once
            if (*region_index == manager->region_num) {
                i = 0;
                j = 0;
                *region_index= 0;
                err= NF2FS_flash_smap_change(NF2FS, manager, NF2FS->pcache, NF2FS->rcache);
                if (err) {
                    NF2FS_ERROR("NF2FS_flash_smap_change error\n");
                    return err;
                }
            }
        }
    } else if (manager->scan_times >= NF2FS_WL_START) {
        // Change sector map with wl module.
        NF2FS_size_t* index= (smap_type == NF2FS_SECTOR_DIR) ? &manager->wl->dir_region_index :
                                                           &manager->wl->bfile_region_index;
        if (manager->wl->changed_region_times == NF2FS_WL_MIGRATE_THRESHOLD) {
            // TODO in the future
            // now we should use LDB instead of GDB, and recalculate the wl_addr
            NF2FS_ASSERT(-1 < 0);
        }

        // TODO in the future
        // we should flush buffered map as LDB into flash
        NF2FS_ASSERT(-1 < 0);

        // update basic messages
        *index= ((*index) + 1) % NF2FS_RAM_REGION_NUM;
        manager->wl->changed_region_times++;
        return err;
    } else {
        NF2FS_ERROR("ERROR in NF2FS_sector_nextsmap\n");
        return NF2FS_ERR_INVAL;
    }
}

// Allocate sequential sectors.
int NF2FS_sectors_find(NF2FS_t *NF2FS, NF2FS_flash_manage_ram_t *manager, NF2FS_size_t num,
                        int smap_type, NF2FS_size_t *begin)
{
    int err= NF2FS_ERR_OK;
    NF2FS_map_ram_t* map= NF2FS_smap_get(manager, smap_type);
    if (!map)
        return NF2FS_ERR_INVAL;

    // get a region if current map does not have a region
    if (map->region == NF2FS_NULL) {
        err= NF2FS_sector_nextsmap(NF2FS, manager, smap_type);
        if (err)
            return err;
    }

    // the max num should less than the region size
    NF2FS_ASSERT(num <= manager->region_size);

    NF2FS_size_t flag_region= map->region;
    while (true) {
        // TODO in the future
        // If we can not find the needed sequential sectors, we should perform GC or WL.
        // LDB in WL is also not considered currently.

        // we have found all sectors we need
        if (NF2FS_find_in_map(NF2FS, manager->region_size, map, num, begin) == num)
            return err;

        // we should change the sector map if we can not find it in current buffer.
        err = NF2FS_sector_nextsmap(NF2FS, manager, smap_type);
        if (err)
            return err;

        // We have scanned all regions but not find
        if (map->region == flag_region) {
            NF2FS_ERROR("NO more space in flash\n");
            return NF2FS_ERR_NOSPC;
        }
    }
}

// transit sector type to smap type
int NF2FS_smap_type_transit(int type)
{
    if (type == NF2FS_SECTOR_MAP || type == NF2FS_SECTOR_WL)
        return NF2FS_SECTOR_META;
    return type;
}

int alloc_num = 0;
void alloc_num_reset() {
    alloc_num = 0;
}
void alloc_num_print() {
    printf("allocate sector number is %d\r\n", alloc_num);
}

// Allocate sector to user.
int NF2FS_sector_alloc(NF2FS_t *NF2FS, NF2FS_flash_manage_ram_t *manager, int sector_type,
                        NF2FS_size_t num, NF2FS_size_t pre_sector, NF2FS_size_t id,
                        NF2FS_size_t father_id, NF2FS_size_t *begin, NF2FS_size_t *etimes)
{
    alloc_num += num;

    int err = NF2FS_ERR_OK;

    // get sequential sectors.
    err= NF2FS_sectors_find(NF2FS, manager, num,
                           NF2FS_smap_type_transit(sector_type), begin);
    if (err)
        return err;

    // Check sector heads and erase if needed.
    NF2FS_size_t sector= *begin;
    NF2FS_size_t cur_etimes= NF2FS_NULL;
    NF2FS_head_t head;
    bool if_erase;
    for (int i= 0; i < num; i++) {
        // erase the sector head if needed, get the old head
        if_erase= NF2FS_sector_erase(NF2FS, sector, &head);

        // cal the erase times
        if (head == NF2FS_NULL) {
            cur_etimes= 0;
        } else if (if_erase) {
            cur_etimes= NF2FS_dhead_dsize(head) + 1;
        } else {
            cur_etimes= NF2FS_dhead_dsize(head);
        }

        // rewrite a sector head
        if (sector_type == NF2FS_SECTOR_BFILE) {
            NF2FS_bfile_sector_flash_t fsector = {
                .head = NF2FS_MKSHEAD(0, NF2FS_STATE_USING, sector_type, 0x3f, cur_etimes),
                .id = id,
                .father_id = father_id,
            };
            err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_SHEAD, sector, 0,
                                  sizeof(NF2FS_bfile_sector_flash_t), &fsector);
            if (err)
                return err;
        } else if (sector_type == NF2FS_SECTOR_DIR) {
            NF2FS_dir_sector_flash_t dsector= {
                .head= NF2FS_MKSHEAD(0, NF2FS_STATE_USING, sector_type, 0x3f, cur_etimes),
                .pre_sector= pre_sector,
                .id= id,
            };
            err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_SHEAD, sector, 0,
                                  sizeof(NF2FS_dir_sector_flash_t), &dsector);
            if (err)
                return err;
        } else if (sector_type == NF2FS_SECTOR_MAP || sector_type == NF2FS_SECTOR_WL) {
            // map does not need to rewrite a sector head
            etimes[i] = cur_etimes;
            continue;
        } else if (sector_type == NF2FS_SECTOR_RESERVE) {
            NF2FS_head_t new_head= NF2FS_MKSHEAD(0, NF2FS_STATE_USING, sector_type, 0x3f, cur_etimes);
            err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_SHEAD, sector, 0,
                                  sizeof(NF2FS_head_t), &new_head);
            if (err)
                return err;
        } else {
            NF2FS_ERROR("WRONG sector type in NF2FS_sector_alloc\n");
            return NF2FS_ERR_INVAL;
        }

        sector++;
    }
    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Erase map operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Set sectors in erase map to 0 so they can reuse in the future.
int NF2FS_emap_set(NF2FS_t *NF2FS, NF2FS_flash_manage_ram_t *manager,
                    NF2FS_size_t begin, NF2FS_size_t num)
{
    int err= NF2FS_ERR_OK;
    NF2FS_map_ram_t* map= NULL;
    bool flush_flag= false;

    // choose the right map, for meta, reserve map, we do not need to flush.
    if (begin / manager->region_size == 0) {
        map= manager->meta_map;
    } else if (begin / manager->region_size == manager->reserve_map->region) {
        map= manager->reserve_map;
    } else {
        map= manager->erase_map;
        flush_flag= true;
    }

    // If current erase map has valid data and it's not the region we
    // want to prog, we should flush it.
    if ((begin / manager->region_size) != map->region &&
        map->index_or_changed && flush_flag) {
        err= NF2FS_erase_map_flush(NF2FS, manager->erase_map,
                                  (begin / manager->region_size));
        if (err)
            return err;
    }

    // Turn bits to 0.
    int i = (begin % manager->region_size) / 32;
    int j = (begin % manager->region_size) % 32;
    for (int k = 0; k < num; k++) {
        map->buffer[i] &= ~(1U << j);
        map->index_or_changed = 1;

        j++;
        if (j == 32) {
            // If j is up to 32.
            i++;
            j= 0;
            if (i == manager->region_size / 32) {
                if (flush_flag) {
                    err= NF2FS_erase_map_flush(NF2FS, manager->erase_map,
                                          map->region+1);
                    if (err)
                        return err;
                } else {
                    // If it is meta or reserve map,
                    // then current sector should be the last.
                    NF2FS_ASSERT(num - k == 1);
                }

                i = 0;
                j = 0;
            }
        }
    }
    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ------------------------------------------------------------    ID map operations    ----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// free the id map
void NF2FS_idmap_free(NF2FS_idmap_ram_t* idmap)
{
    if (idmap) {
        if (idmap->free_map)
            NF2FS_free(idmap->free_map);
        NF2FS_free(idmap);
    }
}

// init the id map
int NF2FS_idmap_init(NF2FS_t* NF2FS, NF2FS_idmap_ram_t** map_addr)
{
    int err = NF2FS_ERR_OK;

    // Allocate memory for map
    NF2FS_idmap_ram_t *idmap = NF2FS_malloc(sizeof(NF2FS_idmap_ram_t));
    if (!idmap) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Allocate free id map.
    err = NF2FS_map_init(NF2FS, &idmap->free_map, idmap->ids_in_buffer / 8);
    if (err) {
        err= NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    *map_addr = idmap;
    return err;

cleanup:
    NF2FS_idmap_free(idmap);
    return err;
}

// flush the id map
int NF2FS_idmap_flush(NF2FS_t* NF2FS, NF2FS_idmap_ram_t* idmap)
{
    int err= NF2FS_ERR_OK;
    err= NF2FS_map_flush(NF2FS, idmap->free_map, idmap->ids_in_buffer / 8,
                        idmap->begin, idmap->off);
    return err;
}

// Assign basic id map message with id addr data.
int NF2FS_idmap_assign(NF2FS_t* NF2FS, NF2FS_idmap_ram_t* id_map, NF2FS_mapaddr_flash_t* map_addr)
{
    id_map->begin= map_addr->begin;
    id_map->off= map_addr->off;
    id_map->etimes= map_addr->erase_times[0];
    id_map->ids_in_buffer= NF2FS_ID_MAX / NF2FS->cfg->region_cnt;
    return NF2FS_ERR_OK;
}

// Change the in-flash id map with old id map.
int NF2FS_idmap_change(NF2FS_t *NF2FS, NF2FS_idmap_ram_t *id_map,
                        NF2FS_cache_ram_t *pcache, NF2FS_cache_ram_t *rcache)
{
    int err = NF2FS_ERR_OK;

    err = NF2FS_cache_flush(NF2FS, pcache);
    if (err)
        return err;

    // find a new place for in-flash bitmap
    NF2FS_size_t old_sector = id_map->begin;
    NF2FS_size_t old_off = id_map->off;
    NF2FS_size_t need_space = 2 * NF2FS_ID_MAX / 8;
    NF2FS_size_t num = NF2FS_alignup(need_space, NF2FS->cfg->sector_size) /
                        NF2FS->cfg->sector_size;
    NF2FS_ASSERT(num == 1);
    if (old_off + need_space >= NF2FS->cfg->sector_size) {
        // We need to find new sector to store map message.
        NF2FS_size_t new_begin = NF2FS_NULL;
        err = NF2FS_sector_alloc(NF2FS, NF2FS->manager, NF2FS_SECTOR_MAP, num, NF2FS_NULL,
                                  NF2FS_NULL, NF2FS_NULL, &new_begin, NULL);
        if (err)
            return err;

        // erase old sector map, currently there is only one sector for id_map
        err= NF2FS_map_sector_erase(NF2FS, id_map->begin, num, &id_map->etimes);
        if (err)
            return err;

        // erase the newly allocated sectors.
        // do not need to write a new sector head because they store maps
        NF2FS_size_t head;
        bool if_erase= NF2FS_sector_erase(NF2FS, new_begin, &head);

        // cal the erase times
        if (head == NF2FS_NULL) {
            id_map->etimes= 0;
        } else if (if_erase) {
            id_map->etimes= NF2FS_dhead_dsize(head) + 1;
        } else {
            id_map->etimes= NF2FS_dhead_dsize(head);
        }

        // Update basic map message.
        id_map->begin= new_begin;
        id_map->off= 0;
    } else {
        // If we do not need new sectors
        id_map->off+= need_space;
    }

    // Read the free id map and remove id map to cache
    need_space /= 2;
    NF2FS_size_t new_sector= id_map->begin;
    NF2FS_off_t off = id_map->off;
    NF2FS_size_t old_sector2 = old_sector + num / 2;
    NF2FS_off_t old_off2= (old_off + need_space >= NF2FS->cfg->sector_size) ? 0 : old_off + need_space;
    while (need_space > 0) {
        NF2FS_size_t size= NF2FS_min(NF2FS->cfg->sector_size - off,
                                   NF2FS_min(need_space, NF2FS->cfg->cache_size));

        // Read free map to pcache
        err = NF2FS_read_to_cache(NF2FS, pcache, old_sector, old_off, size);
        if (err)
            return err;

        // Read remove map to rcache
        err = NF2FS_read_to_cache(NF2FS, rcache, old_sector2, old_off2, size);
        if (err)
            return err;

        // Emerge into one free map.
        NF2FS_size_t *data1 = (NF2FS_size_t *)pcache->buffer;
        NF2FS_size_t *data2 = (NF2FS_size_t *)rcache->buffer;
        for (int i = 0; i < size; i += sizeof(NF2FS_size_t)) {
            // XNOR operation
            *data1 = ~(*data1 ^ *data2);
            data1++;
            data2++;
        }

        // Program the new free map into flash, we use cfg->prog with the head structure
        err = NF2FS->cfg->prog(NF2FS->cfg, new_sector, off, pcache->buffer, size);
        NF2FS_ASSERT(err <= 0);
        if (err) {
            return err;
        }

        off += size;
        old_off += size;
        old_off2 += size;
        if (old_off >= NF2FS->cfg->sector_size) {
            off = 0;
            old_off = 0;
            old_off2 = 0;
            new_sector++;
            old_sector++;
            old_sector2++;
        }
        need_space -= size;
    }

    NF2FS_cache_one(NF2FS, pcache);

    // prog the new map_addr to superblock
    NF2FS_size_t len = sizeof(NF2FS_mapaddr_flash_t) + num * sizeof(NF2FS_size_t);
    NF2FS_mapaddr_flash_t *addr = NF2FS_malloc(len);
    if (addr == NULL) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // fill message
    addr->head = NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_ID_MAP, len);
    addr->begin = id_map->begin;
    addr->off = id_map->off;
    addr->erase_times[0] = id_map->etimes;
    err= NF2FS_prog_in_superblock(NF2FS, NF2FS->superblock, &addr, len);

cleanup:
    // we should finally free the allocated message.
    if (addr != NULL)
        NF2FS_free(addr);
    return err;
}

// prog the id to in-flash map directly
int NF2FS_id_direct_prog(NF2FS_t* NF2FS, NF2FS_size_t id, NF2FS_size_t begin, NF2FS_size_t off)
{
    int err= NF2FS_ERR_OK;

    // Data to prog
    char data= 0xff;
    data= data & ~(1 << (id % 8));

    // Address to prog
    off= off + (id / 8);
    while (off >= NF2FS->cfg->sector_size) {
        begin++;
        off-= NF2FS->cfg->sector_size;
    }

    // prog
    err= NF2FS->cfg->prog(NF2FS->cfg, begin, off, &data, sizeof(char));
    return err;
}

// Allocate a id.
int NF2FS_id_alloc(NF2FS_t *NF2FS, NF2FS_size_t *id)
{
    int err = NF2FS_ERR_OK;

    NF2FS_map_ram_t *idmap = NF2FS->id_map->free_map;
    NF2FS_size_t flag_region= idmap->region;
    while (true) {
        // we can find id in current region
        if (idmap->free_num > 0) {
            err = NF2FS_find_in_map(NF2FS, NF2FS->id_map->ids_in_buffer,
                                     idmap, 1, id);
            if (err < 0)
                return err;

            // prog id into flash directly
            err= NF2FS_id_direct_prog(NF2FS, *id, NF2FS->id_map->begin, NF2FS->id_map->off);
            if (err)
                return err;

            if (*id != NF2FS_NULL)
                return NF2FS_ERR_OK;
        }

        // if we have scanned the whole bitmap without useful id, failed
        NF2FS_size_t new= idmap->region + 1;
        if (flag_region == new)
            return NF2FS_ERR_NOID;

        // get a new id map when we scan to the end
        if (new == NF2FS->cfg->region_cnt) {
            err = NF2FS_idmap_change(NF2FS, NF2FS->id_map,
                                      NF2FS->pcache, NF2FS->rcache);
            if (err)
                return err;
            new = 0;
        }

        // fail to find, turn to the next region
        err= NF2FS_ram_map_change(NF2FS, new, NF2FS->id_map->ids_in_buffer,
                                 idmap, NF2FS->id_map->begin, NF2FS->id_map->off);
        if (err)
            return err;
    }
}

// Free id in id map.
int NF2FS_id_free(NF2FS_t *NF2FS, NF2FS_idmap_ram_t *idmap, NF2FS_size_t id)
{
    int err = NF2FS_ERR_OK;

    // prog the free message to the remove id map directly.
    err= NF2FS_id_direct_prog(NF2FS, id, idmap->begin,
                             idmap->off + (NF2FS_ID_MAX / 8));
    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    basic wl operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// init the wl module
int NF2FS_wl_init(NF2FS_t* NF2FS, NF2FS_wl_ram_t** wl_addr)
{
    // Allocate memory for wl
    NF2FS_wl_ram_t *wl = NF2FS_malloc(sizeof(NF2FS_wl_ram_t));
    if (!wl)
        return NF2FS_ERR_NOMEM;
    memset(wl, 0xff, sizeof(NF2FS_wl_ram_t)); 
    *wl_addr= wl;
    return NF2FS_ERR_OK;
}

// Swap wl message, used for heap sort funtion.
void NF2FS_wl_swap(NF2FS_t *NF2FS, NF2FS_wl_message_t *a, NF2FS_wl_message_t *b)
{
    NF2FS_wl_message_t temp;
    temp.region = a->region;
    temp.etimes = a->etimes;

    a->region = b->region;
    a->etimes = b->etimes;

    b->region = temp.region;
    b->etimes = temp.etimes;
}

// wl message sorted by heaps.
int NF2FS_wl_heaps_sort(NF2FS_t *NF2FS, NF2FS_size_t num, NF2FS_wl_message_t *heaps)
{
    int err = NF2FS_ERR_OK;

    NF2FS_size_t son;
    // Initialize heaps.
    for (int i = num / 2 - 1; i >= 0; i--) {
        for (int j = i; 2 * j + 1 < num; j = son) {
            son = 2 * j + 1;
            if (son + 1 < num &&
                heaps[son].etimes < heaps[son + 1].etimes){
                // If i have a right son, and the erase time of it is smaller
                // than left son. Change to choose the right son.
                son++;
            }

            if (heaps[i].etimes < heaps[son].etimes){
                // If erase time of father is larger than son, then swap.
                NF2FS_wl_swap(NF2FS, &heaps[i], &heaps[son]);
            }
        }
    }

    // Sort the heap into an ascending series.
    NF2FS_wl_message_t last;
    for (int i = num - 1; i >= 0;) {
        last.region = heaps[i].region;
        last.etimes = heaps[i].etimes;
        heaps[i].region = heaps[0].region;
        heaps[i].etimes = heaps[0].etimes;

        i--;
        son = 0;
        for (int j = 0; 2 * j + 1 <= i; j = son) {
            son = 2 * j + 1;
            if (son + 1 <= i &&
                heaps[son].etimes < heaps[son + 1].etimes)
                son++;

            if (last.etimes < heaps[son].etimes) {
                heaps[j].region = heaps[son].region;
                heaps[j].etimes = heaps[son].etimes;
            } else {
                heaps[j].region = last.region;
                heaps[j].etimes = last.etimes;
                last.region = heaps[son].region;
                last.etimes = heaps[son].etimes;
            }
        }
        heaps[son].region = last.region;
        heaps[son].etimes = last.etimes;
    }

    return err;
}

// we should record wl, and map sectors' etimes without sector head.
void NF2FS_wl_map_etimes(NF2FS_t* NF2FS, NF2FS_size_t smap_num, NF2FS_wl_message_t *wlarr)
{
    // the wl, sector map, and id map are all in meta region
    for (int i= 0; i < smap_num; i++) {
        wlarr[0].etimes+= NF2FS->manager->etimes[i];
    }

    wlarr[0].etimes+= NF2FS->id_map->etimes;
    if (NF2FS->manager->wl)
        wlarr[0].etimes+= NF2FS->manager->wl->etimes;
}

// The global region migration module
int NF2FS_global_region_migration(NF2FS_t *NF2FS, NF2FS_flash_manage_ram_t *manager,
                                   NF2FS_wl_message_t *wlarr)
{
    int err = NF2FS_ERR_OK;

    // migrate low etimes regions and high etimes regions
    // reserve the most high etimes regions to exchange the reserve region
    int begin = 0;
    int end= manager->region_num - 2;
    while (begin < end) {
        if (wlarr[begin].region == manager->reserve_map->region)
            begin++;

        if (wlarr[end].region == manager->reserve_map->region)
            end--;
        
        err = NF2FS_region_migration(NF2FS, wlarr[begin].region, wlarr[end].region);
        if (err)
            return err;
        begin++;
        end--;
    }

    // change the reserve region, old data in reserve region should not be migrated
    err= NF2FS_region_migration(NF2FS, wlarr[manager->region_num - 1].region,
                          manager->reserve_map->region);
    if (err)
        return err;

    // reset the reserve region
    manager->reserve_map->region= wlarr[manager->region_num - 1].region;
    manager->reserve_map->free_num= manager->region_size;
    manager->reserve_map->index_or_changed= 0;
    memset(manager->reserve_map->buffer, 0xff, manager->region_size);
    return err;
}

// Sort the wl regions with etimes
int NF2FS_wl_region_sort(NF2FS_t *NF2FS, NF2FS_flash_manage_ram_t *manager)
{
    int err= NF2FS_ERR_OK;
    NF2FS_size_t prog_size= sizeof(NF2FS_size_t) * manager->region_num;
    NF2FS_size_t* arr_flash;
    NF2FS_wladdr_flash_t wladdr;
    int cur_index;

    // allocate a heap for sort
    NF2FS_wl_message_t* wlarr_heap= NF2FS_malloc(manager->region_num * sizeof(NF2FS_wl_message_t));
    if (!wlarr_heap)
        return NF2FS_ERR_NOMEM;

    // init the heap
    for (int i= 0; i < manager->region_num; i++) {
        wlarr_heap[i].region= i;
        wlarr_heap[i].etimes= 0;
    }

    // record the special sectors without sector head
    NF2FS_size_t smap_cnt= NF2FS_alignup(2 * NF2FS->cfg->sector_count / 8, NF2FS->cfg->sector_size) /
                     NF2FS->cfg->sector_size + 2;
    NF2FS_wl_map_etimes(NF2FS, smap_cnt, wlarr_heap);

    // mark these special sectors
    NF2FS_size_t spe_sectors[smap_cnt + 2];
    for (int i= 0; i < smap_cnt; i++)
        spe_sectors[i]= manager->smap_begin + i;

    spe_sectors[smap_cnt]= NF2FS->id_map->begin;
    if (manager->wl) {
        spe_sectors[smap_cnt+1]= manager->wl->begin;
    }else {
        spe_sectors[smap_cnt+1]= NF2FS_NULL;
    }

    // record other normal sectors
    NF2FS_size_t region = 0;
    NF2FS_size_t cnt= 0;
    NF2FS_head_t head;
    bool spe_flag= false;
    for (int i= 0; i < NF2FS->cfg->sector_count; i++) {
        // judge if it's map, wl sector without sector head
        spe_flag= false;
        if (region == 0) {
            for (int j= 0; j < smap_cnt + 2; j++) {
                if (spe_sectors[j] == i) {
                    spe_flag= true;
                    break;
                }
            }
        }

        // if it's normal sector, read and record etimes
        if (~spe_flag) {
            err= NF2FS_direct_read(NF2FS, i, 0, sizeof(NF2FS_head_t), &head);
            if (err)
                goto cleanup;

            err= NF2FS_shead_check(head, NF2FS_STATE_FREE, NF2FS_SECTOR_NOTSURE);
            if (err)
                goto cleanup;
            wlarr_heap[region].etimes += (head == NF2FS_NULL) ? 0 : NF2FS_shead_etimes(head);
        }

        // prepare for the next loop
        cnt++;
        if (cnt == manager->region_size) {
            cnt = 0;
            region++;
        }
    }

    // sort regions with etimes
    err = NF2FS_wl_heaps_sort(NF2FS, manager->region_num, wlarr_heap);
    if (err)
        goto cleanup;

    // if we do not have wl module now, we should create one
    if (!manager->wl) {
        err= NF2FS_wl_init(NF2FS, &manager->wl);
        if (err)
            goto cleanup;
    }

    // do migration
    err = NF2FS_global_region_migration(NF2FS, manager, wlarr_heap);
    if (err)
        goto cleanup;

    // prepare for the new flash and ram wl module
    if (manager->wl->begin == NF2FS_NULL ||
        manager->wl->off + prog_size > NF2FS->cfg->sector_size) {
        // allocate a new sector for wl array
        NF2FS_size_t new_sector= NF2FS_NULL;
        NF2FS_size_t etimes= NF2FS_NULL;
        err = NF2FS_sector_alloc(NF2FS, manager, NF2FS_SECTOR_WL, NF2FS_WL_SECTOR_NUM,
                                NF2FS_NULL, NF2FS_NULL, NF2FS_NULL, &new_sector, &etimes);
        if (err)
            goto cleanup;

        // record the address of the array
        wladdr.head = NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_WL_ADDR, sizeof(NF2FS_wladdr_flash_t));
        wladdr.begin= new_sector;
        wladdr.off= 0;
        wladdr.erase_times= etimes;

        // Add address of wl sectors to superblock.
        err = NF2FS_prog_in_superblock(NF2FS, NF2FS->superblock, &wladdr, sizeof(NF2FS_wladdr_flash_t));
        if (err)
            return err;

        // update in-ram data
        manager->wl->begin= new_sector;
        manager->wl->off= 0;
        manager->wl->etimes= etimes;
    }
    manager->wl->changed_region_times= 0;
    manager->wl->bfile_region_index= 0;
    manager->wl->dir_region_index= 0;

    // update the flash wl module
    arr_flash= (NF2FS_size_t*)wlarr_heap;
    for (int i= 0; i < manager->region_num; i++) {
        arr_flash[i]= wlarr_heap[i].region;
    }
    err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_DATA, manager->wl->begin, manager->wl->off,
                          prog_size, arr_flash);
    if (err)
        goto cleanup;
    manager->wl->off+= prog_size;

    // update the ram wl module, i.e. to be accessed big file regions
    cur_index= 0;
    for (int i= 0; i < manager->region_num; i++) {
        if (NF2FS_region_type(manager->region_map, wlarr_heap[i].region) ==
            NF2FS_SECTOR_BFILE) {
            manager->wl->dir_regions[cur_index]= wlarr_heap[i].region;
            cur_index++;
        }

        if (cur_index == NF2FS_RAM_REGION_NUM)
            break;
    }

    // update the ram wl module, i.e. to be accessed dir regions
    cur_index= 0;
    for (int i= 0; i < manager->region_num; i++) {
        if (NF2FS_region_type(manager->region_map, wlarr_heap[i].region) ==
            NF2FS_SECTOR_DIR) {
            manager->wl->bfile_regions[cur_index]= wlarr_heap[i].region;
            cur_index++;
        }

        if (cur_index == NF2FS_RAM_REGION_NUM)
            break;
    }

cleanup:
    if (wlarr_heap)
        NF2FS_free(wlarr_heap);
    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    Manager operations    ----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// free the ram manager structure
void NF2FS_manager_free(NF2FS_flash_manage_ram_t* manager)
{
    if (manager) {
        NF2FS_region_map_free(manager->region_map);
        if (manager->etimes)
            NF2FS_free(manager->etimes);
        if (manager->wl)
            NF2FS_free(manager->wl);
        if (manager->dir_map)
            NF2FS_free(manager->dir_map);
        if (manager->bfile_map)
            NF2FS_free(manager->bfile_map);
        if (manager->meta_map)
            NF2FS_free(manager->meta_map);
        if (manager->reserve_map)
            NF2FS_free(manager->reserve_map);
        if (manager->erase_map)
            NF2FS_free(manager->erase_map);
        NF2FS_free(manager);
    }
}

// init the ram manager structure
int NF2FS_manager_init(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t** manager_addr)
{
    int err= NF2FS_ERR_OK;
    NF2FS_size_t num;
    NF2FS_size_t smap_len= NF2FS->cfg->sector_count / NF2FS->cfg->region_cnt;

    // malloc memory for manager
    NF2FS_flash_manage_ram_t* manager= NF2FS_malloc(sizeof(NF2FS_flash_manage_ram_t));
    if (!manager) {
        err= NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // basic manager message
    manager->region_num= NF2FS->cfg->region_cnt;
    manager->region_size= NF2FS->cfg->sector_count / NF2FS->cfg->region_cnt;
    manager->wl= NULL;

    // init etimes
    num= NF2FS_alignup(NF2FS->cfg->sector_count / 8, NF2FS->cfg->sector_size) /
                     NF2FS->cfg->sector_size;
    manager->etimes= NF2FS_malloc(num * sizeof(NF2FS_size_t));
    if (!manager->etimes) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // init dir_map
    err= NF2FS_map_init(NF2FS, &manager->dir_map, smap_len);
    if (err) 
        goto cleanup;

    // init bfile_map
    err= NF2FS_map_init(NF2FS, &manager->bfile_map, smap_len);
    if (err) 
        goto cleanup;

    // init meta_map
    err= NF2FS_map_init(NF2FS, &manager->meta_map, smap_len);
    if (err) 
        goto cleanup;

    // init reserve_map
    err= NF2FS_map_init(NF2FS, &manager->reserve_map, smap_len);
    if (err)
        goto cleanup;

    // init erase_map
    err= NF2FS_map_init(NF2FS, &manager->erase_map, smap_len);
    if (err)
        goto cleanup;

    // init region map
    err= NF2FS_region_map_init(manager->region_num, &manager->region_map);
    if (err)
        goto cleanup;

    *manager_addr= manager;
    return err;

cleanup:
    NF2FS_manager_free(manager);
    return err;
}

// Updata all in-ram structure with in-flash commit message.
int NF2FS_init_with_commit(NF2FS_t* NF2FS, NF2FS_commit_flash_t* commit, NF2FS_cache_ram_t* cache)
{
    int err= NF2FS_ERR_OK;

    // update idmap with commit
    NF2FS_size_t temp_region= commit->next_id / NF2FS->id_map->ids_in_buffer;
    err= NF2FS_ram_map_change(NF2FS, temp_region, NF2FS->id_map->ids_in_buffer,
                             NF2FS->id_map->free_map, NF2FS->id_map->begin, NF2FS->id_map->off);
    if (err)
        return err;

    // update scan times
    NF2FS->manager->scan_times= commit->scan_times;

    // update dir map and region map
    temp_region= commit->next_dir_sector / NF2FS->manager->region_size;
    NF2FS->manager->region_map->dir_index= temp_region + 1;
    err= NF2FS_ram_map_change(NF2FS, temp_region, NF2FS->manager->region_size,
                             NF2FS->manager->dir_map, NF2FS->manager->smap_begin, NF2FS->manager->smap_off);
    if (err)
        return err;

    // update big file map and region map
    if (commit->next_bfile_sector != NF2FS_NULL * NF2FS->manager->region_size) {
        temp_region= commit->next_bfile_sector / NF2FS->manager->region_size;
        NF2FS->manager->region_map->bfile_index= temp_region + 1;
        err= NF2FS_ram_map_change(NF2FS, temp_region, NF2FS->manager->region_size,
                                NF2FS->manager->bfile_map, NF2FS->manager->smap_begin, NF2FS->manager->smap_off);
        if (err)
            return err;
    }

    // update meta map
    err= NF2FS_ram_map_change(NF2FS, 0, NF2FS->manager->region_size,
                             NF2FS->manager->meta_map, NF2FS->manager->smap_begin, NF2FS->manager->smap_off);
    if (err)
        return err;

    // update reserve map
    NF2FS->manager->region_map->reserve= commit->reserve_region;
    err= NF2FS_ram_map_change(NF2FS, commit->reserve_region, NF2FS->manager->region_size,
                             NF2FS->manager->reserve_map, NF2FS->manager->smap_begin, NF2FS->manager->smap_off);
    if (err)
        return err;

    // if wl hasn't been build, return directly
    if (!NF2FS->manager->wl)
        return err;

    // update wl message
    NF2FS_size_t dir_index= 0;
    NF2FS_size_t bfile_index= 0;
    NF2FS_size_t total_size= sizeof(NF2FS_size_t) * NF2FS->cfg->region_cnt;
    while (total_size > 0) {
        // read wl message to cache
        NF2FS_size_t read_size= NF2FS_min(total_size, NF2FS->cfg->cache_size);
        err= NF2FS_direct_read(NF2FS, NF2FS->manager->wl->begin, NF2FS->manager->wl->off - total_size,
                              read_size, cache->buffer);
        if (err)
            return err;
        total_size-= read_size;

        // add regions with less etimes to wl module
        NF2FS_size_t max_loop= NF2FS->cfg->cache_size / sizeof(NF2FS_size_t);
        NF2FS_size_t* cur_region= (NF2FS_size_t *)cache->buffer;
        for (int i= 0; i < max_loop; i++) {
            int region_type= NF2FS_region_type(NF2FS->manager->region_map, cur_region[i]);
            if (region_type == NF2FS_SECTOR_BFILE) {
                NF2FS->manager->wl->bfile_regions[bfile_index]= cur_region[i];
                bfile_index++;
            } else if (region_type == NF2FS_SECTOR_DIR) {
                NF2FS->manager->wl->dir_regions[dir_index]= cur_region[i];
                dir_index++;
            }

            // add finished, return
            if (bfile_index == NF2FS_RAM_REGION_NUM && dir_index == NF2FS_RAM_REGION_NUM) {
                NF2FS_cache_one(NF2FS, cache);
                return err;
            }
        }
    }

    // add finished, return
    NF2FS_cache_one(NF2FS, cache);
    return err;
}

// Init and assign in-ram superblock structure.
int NF2FS_super_init(NF2FS_t *NF2FS, NF2FS_superblock_ram_t **super_addr)
{
    NF2FS_superblock_ram_t *superblock = NF2FS_malloc(sizeof(NF2FS_superblock_ram_t));
    if (!superblock)
        return NF2FS_ERR_NOMEM;

    superblock->sector = NF2FS_NULL;
    *super_addr= superblock;
    
    return NF2FS_ERR_OK;
}
