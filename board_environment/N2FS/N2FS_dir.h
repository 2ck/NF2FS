/**
 * Dir related operations.
 */
#ifndef N2FS_DIR_H
#define N2FS_DIR_H

#include "N2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Free specific dir in dir list.
int N2FS_dir_free(N2FS_dir_ram_t* list, N2FS_dir_ram_t* dir);

// Find the needed name address in the dir.
int N2FS_dtraverse_name(N2FS_t* N2FS, N2FS_size_t begin_sector, char* name, N2FS_size_t namelen, int file_type, N2FS_tree_entry_ram_t* entry);

// find file's data or index in the dir
int N2FS_dtraverse_data(N2FS_t* N2FS, N2FS_file_ram_t* file);

// delete all big files in current dir
int N2FS_dtraverse_bfile_delete(N2FS_t* N2FS, N2FS_dir_ram_t* dir);

// GC while traversing the dir
int N2FS_dtraverse_gc(N2FS_t* N2FS, N2FS_dir_ram_t* dir);

// prog node to the dir
int N2FS_dir_prog(N2FS_t* N2FS, N2FS_dir_ram_t* dir, void* buffer, N2FS_size_t len);

// set bits in erase map to old.
int N2FS_dir_old(N2FS_t* N2FS, N2FS_size_t tail);

// Update dir entry from its father dir.
int N2FS_dir_update(N2FS_t* N2FS, N2FS_dir_ram_t* dir);

// open dir
int N2FS_dir_lowopen(N2FS_t* N2FS, N2FS_size_t tail, N2FS_size_t id, N2FS_size_t father_id, N2FS_size_t name_sector, N2FS_size_t name_off,
                     N2FS_dir_ram_t** dir_addr, N2FS_cache_ram_t* cache);

// find opened dir with its id.
int N2FS_open_dir_find(N2FS_t* N2FS, N2FS_size_t id, N2FS_dir_ram_t** dir_addr);

// create a new dir
int N2FS_create_dir(N2FS_t* N2FS, N2FS_dir_ram_t* father_dir, N2FS_dir_ram_t** dir_addr, char* name, N2FS_size_t namelen);

#ifdef __cplusplus
}
#endif

#endif
