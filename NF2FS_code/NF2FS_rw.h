/**
 * The basic read, write, erase operations with/without cache
 */

#ifndef NF2FS_RW_H
#define NF2FS_RW_H

#include "NF2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

void in_place_size_reset(void);

void in_place_size_print(void);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------    Auxiliary cache functions    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Init the cache.
int NF2FS_cache_init(NF2FS_t* NF2FS, NF2FS_cache_ram_t** cache_addr, NF2FS_size_t buffer_size);

// maybe used in file sync function
void NF2FS_cache_buffer_reset(NF2FS_t* NF2FS, NF2FS_cache_ram_t* cache);

void NF2FS_cache_drop(NF2FS_t* NF2FS, NF2FS_cache_ram_t* cache);

void NF2FS_cache_one(NF2FS_t* NF2FS, NF2FS_cache_ram_t* cache);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------    Prog/Erase cache functions    ------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */
int NF2FS_cache_flush(NF2FS_t* NF2FS, NF2FS_cache_ram_t* pcache);

int NF2FS_cache_read(NF2FS_t* NF2FS, const NF2FS_cache_ram_t* pcache, NF2FS_cache_ram_t* rcache, NF2FS_size_t sector, NF2FS_off_t off, void* buffer, NF2FS_size_t size);

int NF2FS_cache_prog(NF2FS_t* NF2FS, NF2FS_cache_ram_t* pcache, NF2FS_cache_ram_t* rcache, NF2FS_size_t sector, NF2FS_off_t off, void* buffer, NF2FS_size_t size);

// different to cache_read, the data is read into cache, not in a buffer
int NF2FS_read_to_cache(NF2FS_t* NF2FS, NF2FS_cache_ram_t* cache, NF2FS_size_t sector, NF2FS_off_t off, NF2FS_size_t size);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * --------------------------------------------------------    Prog/Erase without cache    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// read data directly from NOR flash, do not need to change any metadata
int NF2FS_direct_read(NF2FS_t* NF2FS, NF2FS_size_t sector, NF2FS_off_t off, NF2FS_size_t size, void* buffer);

// when prog directly, we should sync cache message
void NF2FS_dprog_cache_sync(NF2FS_t* NF2FS, NF2FS_cache_ram_t* cache, NF2FS_size_t sector, NF2FS_off_t off, NF2FS_size_t size, void* buffer, int dp_type, bool if_written_flag);

// When finishing prog, we should changed state in shead or flag/type in dhead
int NF2FS_head_validate(NF2FS_t* NF2FS, NF2FS_size_t sector, NF2FS_size_t off, NF2FS_head_t head_flag);

// directly prog a sector header or a data according to data_type
// should validate the written flag for dhead
int NF2FS_direct_prog(NF2FS_t* NF2FS, NF2FS_size_t data_type, NF2FS_size_t sector, NF2FS_off_t off, NF2FS_size_t size, void* buffer);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------    More complex operations    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// prog data into superblock
int NF2FS_prog_in_superblock(NF2FS_t* NF2FS, NF2FS_superblock_ram_t* super, void* buffer, NF2FS_size_t size);

// Prog all metadata into a new superblock when init fs or change superbleok.
int NF2FS_superblock_change(NF2FS_t* NF2FS, NF2FS_superblock_ram_t* super, NF2FS_cache_ram_t* pcache, bool if_commit);

// find valid sectors in the region, write the bitmap to buffer
int NF2FS_find_sectors_in_region(NF2FS_t* NF2FS, NF2FS_size_t region, uint32_t* buffer);

// Calculate the number of valid bits(0) in cache buffer.
NF2FS_size_t NF2FS_cal_valid_bits(NF2FS_cache_ram_t* cache);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------    Delete/erase operations    ------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// delete data and add the old_space for father
int NF2FS_data_delete(NF2FS_t* NF2FS, NF2FS_size_t father_id, NF2FS_size_t sector, NF2FS_off_t off, NF2FS_size_t size);

// set shead to delete type, change the remove bitmap
int NF2FS_sequen_sector_old(NF2FS_t* NF2FS, NF2FS_size_t begin, NF2FS_size_t num);

// similar to NF2FS_sequen_sector_old, but should traverse indexs to sectors
int NF2FS_bfile_sector_old(NF2FS_t* NF2FS, NF2FS_bfile_index_ram_t* index, NF2FS_size_t num);

// erase a normal sector, should change corresponding sector header (i.e., reprog etimes)
bool NF2FS_sector_erase(NF2FS_t* NF2FS, NF2FS_size_t sector, NF2FS_head_t* head);

// erase map sector without shead
int NF2FS_map_sector_erase(NF2FS_t* NF2FS, NF2FS_size_t begin, NF2FS_size_t num, NF2FS_size_t* etimes);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * --------------------------------------------------------    GC & WL operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// GC for a dir
int NF2FS_dir_gc(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir);

// GC for big file
int NF2FS_bfile_gc(NF2FS_t* NF2FS, NF2FS_file_ram_t* file);

// migrate the two regions with reserve region
int NF2FS_region_migration(NF2FS_t* NF2FS, NF2FS_size_t region_1, NF2FS_size_t region_2);

#ifdef __cplusplus
}
#endif

#endif
