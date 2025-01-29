/**
 * Hash tree related operations.
 */
#ifndef NF2FS_TREE_H
#define NF2FS_TREE_H

#include "NF2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ------------------------------------------------------------    Hash calculation    -----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// cal the hash value of the name, return value is the hash
NF2FS_size_t NF2FS_hash(uint8_t* buffer, NF2FS_size_t len);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    basic tree operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// init the tree structure
int NF2FS_tree_init(NF2FS_t* NF2FS, NF2FS_tree_ram_t** tree_addr);

// add a tree entry into the tree
int NF2FS_tree_entry_add(NF2FS_tree_ram_t* tree, NF2FS_size_t father_id, NF2FS_size_t id, NF2FS_size_t name_sector, NF2FS_size_t name_off, NF2FS_size_t tail,
                        char* name, int namelen);

// update a tree entry into the tree
int NF2FS_tree_entry_update(NF2FS_tree_ram_t* tree, NF2FS_size_t id, NF2FS_size_t name_sector, NF2FS_size_t name_off, NF2FS_size_t tail);

// remove a tree entry in the tree
int NF2FS_tree_entry_remove(NF2FS_tree_ram_t* tree, NF2FS_size_t id);

// find a tree entry in the tree with id
int NF2FS_tree_entry_id_find(NF2FS_tree_ram_t* tree, NF2FS_size_t id, NF2FS_size_t* index);

// find a tree entry in the tree with name
int NF2FS_tree_entry_name_find(NF2FS_t* NF2FS, char* name, NF2FS_size_t namelen, NF2FS_size_t father_id, NF2FS_size_t* index);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    further tree operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Retur the father path of current path.
// e.g., return /bin in path /usr/bin
char* NF2FS_name_in_path(char* path);

// find the tree entry of path's father
int NF2FS_father_dir_find(NF2FS_t* NF2FS, char* path, NF2FS_tree_entry_ram_t** entry_addr);

#ifdef __cplusplus
}
#endif

#endif
