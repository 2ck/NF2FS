/**
 * The basic read, write, erase operations with/without cache
 */

#ifndef N2FS_RW_H
#define N2FS_RW_H

#include "N2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------    Auxiliary cache functions    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Init the cache.
int N2FS_cache_init(N2FS_t* N2FS, N2FS_cache_ram_t** cache_addr, N2FS_size_t buffer_size);

// maybe used in file sync function
void N2FS_cache_buffer_reset(N2FS_t* N2FS, N2FS_cache_ram_t* cache);

void N2FS_cache_drop(N2FS_t* N2FS, N2FS_cache_ram_t* cache);

void N2FS_cache_one(N2FS_t* N2FS, N2FS_cache_ram_t* cache);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------    Prog/Erase cache functions    ------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */
int N2FS_cache_flush(N2FS_t* N2FS, N2FS_cache_ram_t* pcache);

int N2FS_cache_read(N2FS_t* N2FS, const N2FS_cache_ram_t* pcache, N2FS_cache_ram_t* rcache, N2FS_size_t sector, N2FS_off_t off, void* buffer, N2FS_size_t size);

int N2FS_cache_prog(N2FS_t* N2FS, N2FS_cache_ram_t* pcache, N2FS_cache_ram_t* rcache, N2FS_size_t sector, N2FS_off_t off, void* buffer, N2FS_size_t size);

// different to cache_read, the data is read into cache, not in a buffer
int N2FS_read_to_cache(N2FS_t* N2FS, N2FS_cache_ram_t* cache, N2FS_size_t sector, N2FS_off_t off, N2FS_size_t size);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * --------------------------------------------------------    Prog/Erase without cache    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// read data directly from NOR flash, do not need to change any metadata
int N2FS_direct_read(N2FS_t* N2FS, N2FS_size_t sector, N2FS_off_t off, N2FS_size_t size, void* buffer);

// when prog directly, we should sync cache message
void N2FS_dprog_cache_sync(N2FS_t* N2FS, N2FS_cache_ram_t* cache, N2FS_size_t sector, N2FS_off_t off, N2FS_size_t size, void* buffer, int dp_type, bool if_written_flag);

// When finishing prog, we should changed state in shead or flag/type in dhead
int N2FS_head_validate(N2FS_t* N2FS, N2FS_size_t sector, N2FS_size_t off, N2FS_head_t head_flag);

// directly prog a sector header or a data according to data_type
// should validate the written flag for dhead
int N2FS_direct_prog(N2FS_t* N2FS, N2FS_size_t data_type, N2FS_size_t sector, N2FS_off_t off, N2FS_size_t size, void* buffer);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------    More complex operations    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// prog data into superblock
int N2FS_prog_in_superblock(N2FS_t* N2FS, N2FS_superblock_ram_t* super, void* buffer, N2FS_size_t size);

// Prog all metadata into a new superblock when init fs or change superbleok.
int N2FS_superblock_change(N2FS_t* N2FS, N2FS_superblock_ram_t* super, N2FS_cache_ram_t* pcache, bool if_commit);

// find valid sectors in the region, write the bitmap to buffer
int N2FS_find_sectors_in_region(N2FS_t* N2FS, N2FS_size_t region, uint32_t* buffer);

// Calculate the number of valid bits(0) in cache buffer.
N2FS_size_t N2FS_cal_valid_bits(N2FS_cache_ram_t* cache);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------    Delete/erase operations    ------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// delete data and add the old_space for father
int N2FS_data_delete(N2FS_t* N2FS, N2FS_size_t father_id, N2FS_size_t sector, N2FS_off_t off, N2FS_size_t size);

// set shead to delete type, change the remove bitmap
int N2FS_sequen_sector_old(N2FS_t* N2FS, N2FS_size_t begin, N2FS_size_t num);

// similar to N2FS_sequen_sector_old, but should traverse indexs to sectors
int N2FS_bfile_sector_old(N2FS_t* N2FS, N2FS_bfile_index_ram_t* index, N2FS_size_t num);

// erase a normal sector, should change corresponding sector header (i.e., reprog etimes)
bool N2FS_sector_erase(N2FS_t* N2FS, N2FS_size_t sector, N2FS_head_t* head);

// erase map sector without shead
int N2FS_map_sector_erase(N2FS_t* N2FS, N2FS_size_t begin, N2FS_size_t num, N2FS_size_t* etimes);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * --------------------------------------------------------    GC & WL operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// GC for a dir
int N2FS_dir_gc(N2FS_t* N2FS, N2FS_dir_ram_t* dir);

// GC for big file
int N2FS_bfile_gc(N2FS_t* N2FS, N2FS_file_ram_t* file);

// migrate the two regions with reserve region
int N2FS_region_migration(N2FS_t* N2FS, N2FS_size_t region_1, N2FS_size_t region_2);

void in_place_size_reset(void);

void in_place_size_print(void);

#ifdef __cplusplus
}
#endif

#endif
