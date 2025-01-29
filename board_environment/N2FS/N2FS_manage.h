/**
 * The basic manager operations of N2FS
 */

#ifndef N2FS_MANAGE_H
#define N2FS_MANAGE_H

#include "N2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    Manager operations    ----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Init and assign in-ram superblock structure.
int N2FS_super_init(N2FS_t* N2FS, N2FS_superblock_ram_t** super_addr);

// init the ram manager structure
int N2FS_manager_init(N2FS_t* N2FS, N2FS_flash_manage_ram_t** manager_addr);

// free the ram manager structure
void N2FS_manager_free(N2FS_flash_manage_ram_t* manager);

// Updata all in-ram structure with in-flash commit message.
int N2FS_init_with_commit(N2FS_t* N2FS, N2FS_commit_flash_t* commit, N2FS_cache_ram_t* cache);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------    Basic region operations    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// assign messages for in-ram region map with in-flash region map
void N2FS_region_map_assign(N2FS_t* N2FS, N2FS_region_map_ram_t* ram_region, N2FS_region_map_flash_t* flash_region, N2FS_size_t sector, N2FS_size_t free_off);

// flush region map to NOR flash.
int N2FS_region_map_flush(N2FS_t* N2FS, N2FS_region_map_ram_t* region_map);

// alloc a new region to dir or file
int N2FS_region_alloc(N2FS_t* N2FS, N2FS_region_map_ram_t* ram_region, N2FS_region_map_flash_t* flash_region, N2FS_size_t sector, N2FS_size_t free_off);

// remove a region from origin region type, may be the region_change or no need in the future
int N2FS_remove_region(N2FS_t* N2FS, N2FS_region_map_ram_t* region_map, int type, N2FS_size_t region);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Basic map operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// flush sector map to flash
int N2FS_map_flush(N2FS_t* N2FS, N2FS_map_ram_t* map, N2FS_size_t buffer_len, N2FS_size_t map_begin, N2FS_size_t map_off);

// the in-ram map change function, change to the next region.
int N2FS_ram_map_change(N2FS_t* N2FS, N2FS_size_t region, N2FS_size_t bits_in_buffer, N2FS_map_ram_t* map, N2FS_size_t map_begin, N2FS_size_t map_off);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Sector map operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// init in-RAM sector map
int N2FS_smap_init(N2FS_t* N2FS, N2FS_flash_manage_ram_t* manager, N2FS_size_t num);

// Assign basic sector map message with sector addr data.
int N2FS_smap_assign(N2FS_t* N2FS, N2FS_flash_manage_ram_t* manager, N2FS_mapaddr_flash_t* map_addr, N2FS_size_t num);

// Flush all sector map messages into flash.
// should only used in unmount or change the in-NOR map
int N2FS_smap_flush(N2FS_t* N2FS, N2FS_flash_manage_ram_t* manager);

// Find next region of sector map to scan.
int N2FS_sector_nextsmap(N2FS_t* N2FS, N2FS_flash_manage_ram_t* manager, int type);

// Allocate sequential sectors.
int N2FS_sectors_find(N2FS_t* N2FS, N2FS_flash_manage_ram_t* manager, N2FS_size_t num, int type, N2FS_size_t* begin);

// Allocate sector to user.
int N2FS_sector_alloc(N2FS_t* N2FS, N2FS_flash_manage_ram_t* manager, int sector_type, N2FS_size_t num, N2FS_size_t pre_sector, N2FS_size_t id,
                      N2FS_size_t father_id, N2FS_size_t* begin, N2FS_size_t* etimes);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Erase map operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Set sectors in erase map to 0 so they can reuse in the future.
int N2FS_emap_set(N2FS_t* N2FS, N2FS_flash_manage_ram_t* manager, N2FS_size_t begin, N2FS_size_t num);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ------------------------------------------------------------    ID map operations    ----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// free the id map
void N2FS_idmap_free(N2FS_idmap_ram_t* idmap);

// init the id map
int N2FS_idmap_init(N2FS_t* N2FS, N2FS_idmap_ram_t** map_addr);

// Assign basic id map message with id addr data.
int N2FS_idmap_assign(N2FS_t* N2FS, N2FS_idmap_ram_t* id_map, N2FS_mapaddr_flash_t* map_addr);

// Alloc an id, should flush to NOR flash immediately for consistency, maybe a new function
int N2FS_id_alloc(N2FS_t* N2FS, N2FS_size_t* id);

// Free an id, should flush to NOR flash immediately for consistency
int N2FS_id_free(N2FS_t* N2FS, N2FS_idmap_ram_t* idmap, N2FS_size_t id);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    basic wl operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// init the wl module
int N2FS_wl_init(N2FS_t* N2FS, N2FS_wl_ram_t** wl_addr);

#ifdef __cplusplus
}
#endif

#endif
