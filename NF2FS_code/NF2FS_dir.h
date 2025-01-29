/**
 * Dir related operations.
 */
#ifndef NF2FS_DIR_H
#define NF2FS_DIR_H

#include "NF2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Free specific dir in dir list.
int NF2FS_dir_free(NF2FS_dir_ram_t* list, NF2FS_dir_ram_t* dir);

// Find the needed name address in the dir.
int NF2FS_dtraverse_name(NF2FS_t* NF2FS, NF2FS_size_t begin_sector, char* name, NF2FS_size_t namelen, int file_type, NF2FS_tree_entry_ram_t* entry);

// find file's data or index in the dir
int NF2FS_dtraverse_data(NF2FS_t* NF2FS, NF2FS_file_ram_t* file);

// delete all big files in current dir
int NF2FS_dtraverse_bfile_delete(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir);

// GC while traversing the dir
int NF2FS_dtraverse_gc(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir);

// prog node to the dir
int NF2FS_dir_prog(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir, void* buffer, NF2FS_size_t len);

// set bits in erase map to old.
int NF2FS_dir_old(NF2FS_t* NF2FS, NF2FS_size_t tail);

// Update dir entry from its father dir.
int NF2FS_dir_update(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir);

// open dir
int NF2FS_dir_lowopen(NF2FS_t* NF2FS, NF2FS_size_t tail, NF2FS_size_t id, NF2FS_size_t father_id, NF2FS_size_t name_sector, NF2FS_size_t name_off,
                     NF2FS_dir_ram_t** dir_addr, NF2FS_cache_ram_t* cache);

// find opened dir with its id.
int NF2FS_open_dir_find(NF2FS_t* NF2FS, NF2FS_size_t id, NF2FS_dir_ram_t** dir_addr);

// create a new dir
int NF2FS_create_dir(NF2FS_t* NF2FS, NF2FS_dir_ram_t* father_dir, NF2FS_dir_ram_t** dir_addr, char* name, NF2FS_size_t namelen);

#ifdef __cplusplus
}
#endif

#endif
