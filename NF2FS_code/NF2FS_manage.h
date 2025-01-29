/**
 * The basic manager operations of NF2FS
 */

#ifndef NF2FS_MANAGE_H
#define NF2FS_MANAGE_H

#include "NF2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

void alloc_num_reset();

void alloc_num_print();

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    Manager operations    ----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Init and assign in-ram superblock structure.
int NF2FS_super_init(NF2FS_t* NF2FS, NF2FS_superblock_ram_t** super_addr);

// init the ram manager structure
int NF2FS_manager_init(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t** manager_addr);

// free the ram manager structure
void NF2FS_manager_free(NF2FS_flash_manage_ram_t* manager);

// Updata all in-ram structure with in-flash commit message.
int NF2FS_init_with_commit(NF2FS_t* NF2FS, NF2FS_commit_flash_t* commit, NF2FS_cache_ram_t* cache);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------    Basic region operations    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// assign messages for in-ram region map with in-flash region map
void NF2FS_region_map_assign(NF2FS_t* NF2FS, NF2FS_region_map_ram_t* ram_region, NF2FS_region_map_flash_t* flash_region, NF2FS_size_t sector, NF2FS_size_t free_off);

// flush region map to NOR flash.
int NF2FS_region_map_flush(NF2FS_t* NF2FS, NF2FS_region_map_ram_t* region_map);

// alloc a new region to dir or file
int NF2FS_region_alloc(NF2FS_t* NF2FS, NF2FS_region_map_ram_t* ram_region, NF2FS_region_map_flash_t* flash_region, NF2FS_size_t sector, NF2FS_size_t free_off);

// remove a region from origin region type, may be the region_change or no need in the future
int NF2FS_remove_region(NF2FS_t* NF2FS, NF2FS_region_map_ram_t* region_map, int type, NF2FS_size_t region);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Basic map operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// flush sector map to flash
int NF2FS_map_flush(NF2FS_t* NF2FS, NF2FS_map_ram_t* map, NF2FS_size_t buffer_len, NF2FS_size_t map_begin, NF2FS_size_t map_off);

// the in-ram map change function, change to the next region.
int NF2FS_ram_map_change(NF2FS_t* NF2FS, NF2FS_size_t region, NF2FS_size_t bits_in_buffer, NF2FS_map_ram_t* map, NF2FS_size_t map_begin, NF2FS_size_t map_off);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Sector map operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// init in-RAM sector map
int NF2FS_smap_init(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t* manager, NF2FS_size_t num);

// Assign basic sector map message with sector addr data.
int NF2FS_smap_assign(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t* manager, NF2FS_mapaddr_flash_t* map_addr, NF2FS_size_t num);

// Flush all sector map messages into flash.
// should only used in unmount or change the in-NOR map
int NF2FS_smap_flush(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t* manager);

// Find next region of sector map to scan.
int NF2FS_sector_nextsmap(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t* manager, int type);

// Allocate sequential sectors.
int NF2FS_sectors_find(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t* manager, NF2FS_size_t num, int type, NF2FS_size_t* begin);

// Allocate sector to user.
int NF2FS_sector_alloc(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t* manager, int sector_type, NF2FS_size_t num, NF2FS_size_t pre_sector, NF2FS_size_t id,
                      NF2FS_size_t father_id, NF2FS_size_t* begin, NF2FS_size_t* etimes);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Erase map operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Set sectors in erase map to 0 so they can reuse in the future.
int NF2FS_emap_set(NF2FS_t* NF2FS, NF2FS_flash_manage_ram_t* manager, NF2FS_size_t begin, NF2FS_size_t num);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ------------------------------------------------------------    ID map operations    ----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// free the id map
void NF2FS_idmap_free(NF2FS_idmap_ram_t* idmap);

// init the id map
int NF2FS_idmap_init(NF2FS_t* NF2FS, NF2FS_idmap_ram_t** map_addr);

// Assign basic id map message with id addr data.
int NF2FS_idmap_assign(NF2FS_t* NF2FS, NF2FS_idmap_ram_t* id_map, NF2FS_mapaddr_flash_t* map_addr);

// Alloc an id, should flush to NOR flash immediately for consistency, maybe a new function
int NF2FS_id_alloc(NF2FS_t* NF2FS, NF2FS_size_t* id);

// Free an id, should flush to NOR flash immediately for consistency
int NF2FS_id_free(NF2FS_t* NF2FS, NF2FS_idmap_ram_t* idmap, NF2FS_size_t id);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    basic wl operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// init the wl module
int NF2FS_wl_init(NF2FS_t* NF2FS, NF2FS_wl_ram_t** wl_addr);

#ifdef __cplusplus
}
#endif

#endif
