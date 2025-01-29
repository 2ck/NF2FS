/**
 * Hash tree related operations.
 */
#ifndef N2FS_TREE_H
#define N2FS_TREE_H

#include "N2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ------------------------------------------------------------    Hash calculation    -----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// cal the hash value of the name, return value is the hash
N2FS_size_t N2FS_hash(uint8_t* buffer, N2FS_size_t len);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    basic tree operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// init the tree structure
int N2FS_tree_init(N2FS_t* N2FS, N2FS_tree_ram_t** tree_addr);

// add a tree entry into the tree
int N2FS_tree_entry_add(N2FS_tree_ram_t* tree, N2FS_size_t father_id, N2FS_size_t id, N2FS_size_t name_sector, N2FS_size_t name_off, N2FS_size_t tail,
                        char* name, int namelen);

// update a tree entry into the tree
int N2FS_tree_entry_update(N2FS_tree_ram_t* tree, N2FS_size_t id, N2FS_size_t name_sector, N2FS_size_t name_off, N2FS_size_t tail);

// remove a tree entry in the tree
int N2FS_tree_entry_remove(N2FS_tree_ram_t* tree, N2FS_size_t id);

// find a tree entry in the tree with id
int N2FS_tree_entry_id_find(N2FS_tree_ram_t* tree, N2FS_size_t id, N2FS_size_t* index);

// find a tree entry in the tree with name
int N2FS_tree_entry_name_find(N2FS_t* N2FS, char* name, N2FS_size_t namelen, N2FS_size_t father_id, N2FS_size_t* index);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    further tree operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Retur the father path of current path.
// e.g., return /bin in path /usr/bin
char* N2FS_name_in_path(char* path);

// find the tree entry of path's father
int N2FS_father_dir_find(N2FS_t* N2FS, char* path, N2FS_tree_entry_ram_t** entry_addr);

#ifdef __cplusplus
}
#endif

#endif
