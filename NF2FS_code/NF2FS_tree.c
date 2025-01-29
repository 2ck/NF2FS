/**
 * tree related operations.
 */
#include "NF2FS_tree.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "NF2FS.h"
#include "NF2FS_dir.h"
#include "NF2FS_rw.h"
#include "NF2FS_util.h"

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ------------------------------------------------------------    Hash calculation    -----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// cal the hash value of the name, return value is the hash
NF2FS_size_t NF2FS_hash(uint8_t* buffer, NF2FS_size_t len)
{
    NF2FS_size_t hash = 5381;
    uint8_t *data = (uint8_t *)buffer;

    while (len > 0)
    {
        hash = ((hash << 5) + hash) + *data;
        data++;
        len--;
    }
    return hash;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    basic tree operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// init the tree structure
int NF2FS_tree_init(NF2FS_t* NF2FS, NF2FS_tree_ram_t** tree_addr)
{
    int err= NF2FS_ERR_OK;
    NF2FS_ASSERT(NF2FS->cfg->cache_size > sizeof(NF2FS_tree_entry_ram_t));

    // malloc for tree
    NF2FS_tree_ram_t* tree= NF2FS_malloc(sizeof(NF2FS_tree_ram_t));
    if (!tree) {
        err= NF2FS_ERR_NOMEM;
        return err;
    }

    // malloc for tree entry
    tree->entry_num= NF2FS->cfg->cache_size / sizeof(NF2FS_tree_entry_ram_t);
    tree->tree_array= NF2FS_malloc(NF2FS->cfg->cache_size);
    if (!tree->tree_array) {
        NF2FS_free(tree);
        err= NF2FS_ERR_NOMEM;
        return err;
    }

    memset(tree->tree_array, 0xff, NF2FS->cfg->cache_size);
    *tree_addr = tree;
    return err;
}

// find the free space for a tree entry
int NF2FS_tree_entry_findfree(NF2FS_tree_ram_t* tree)
{
    for (int i= 0; i < tree->entry_num; i++) {
        if (tree->tree_array[i].id == NF2FS_NULL)
            return i;
    }
    return NF2FS_NULL;
}

// add a tree entry into the tree
int NF2FS_tree_entry_add(NF2FS_tree_ram_t* tree, NF2FS_size_t father_id, NF2FS_size_t id,
                        NF2FS_size_t name_sector, NF2FS_size_t name_off, NF2FS_size_t tail,
                        char* name, int namelen)
{
    NF2FS_size_t err= NF2FS_ERR_OK;
    NF2FS_size_t index= NF2FS_NULL;

    // return directly if the entry is already in tree
    for (int i= 0; i < tree->entry_num; i++) {
        if (tree->tree_array[i].id == id)
            return err;
    }

    // find a free space to add the tree entry
    index = NF2FS_tree_entry_findfree(tree);

    // the tree entry is already exist in the tree
    // there is no more space in the tree
    // TODO, a replace stragety may be used in the future
    if (index == NF2FS_NULL)
        return err;

    // update the tree entry message
    tree->tree_array[index].id= id;
    tree->tree_array[index].father_id= father_id;
    tree->tree_array[index].name_sector= name_sector;
    tree->tree_array[index].name_off= name_off;
    tree->tree_array[index].tail_sector= tail;
    if (namelen <= NF2FS_ENTRY_NAME_LEN) {
        memcpy(tree->tree_array[index].data.name, name, namelen);
    }else {
        tree->tree_array[index].data.hash = NF2FS_hash((uint8_t*)name, namelen);
    }

    return err;
}

// judge if the tree entry is valid
bool inline NF2FS_tree_entry_isvalid(NF2FS_tree_ram_t* tree, NF2FS_size_t tree_index, NF2FS_size_t father_id)
{
    return (tree->tree_array[tree_index].id != NF2FS_NULL &&
            tree->tree_array[tree_index].father_id == father_id);
}

// Compare name in tree entry with name in path.
// return true if is the same
bool NF2FS_ename_isequal(NF2FS_t *NF2FS, NF2FS_tree_entry_ram_t *entry, char *name, NF2FS_size_t namelen)
{
    NF2FS_size_t err= NF2FS_ERR_OK;
    
    // If name is in ram tree, compare directly
    if (namelen <= NF2FS_ENTRY_NAME_LEN)
        return memcmp(name, entry->data.name, namelen) ? false : true;

    // If name is too long, compare hash first
    NF2FS_hash_t hash = NF2FS_hash((uint8_t *)name, namelen);
    if (hash == entry->data.hash) {
        // If hash is the same, compare name from NOR flash
        // TODO in the future, this may be expensive when namelen is too large
        char temp_buffer[namelen];
        err= NF2FS_direct_read(NF2FS, entry->name_sector, entry->name_off + sizeof(NF2FS_head_t),
                              namelen, temp_buffer);
        if (err)
            return err;
        return memcmp(name, temp_buffer, namelen) ? false : true;
    }
    return false;
}

// find a tree entry in the tree with name
int NF2FS_tree_entry_name_find(NF2FS_t* NF2FS, char* name, NF2FS_size_t namelen, NF2FS_size_t father_id, NF2FS_size_t* index)
{
    // find a free space to add the tree entry
    for (int i= 0; i < NF2FS->ram_tree->entry_num; i++) {
        // if current entry does not have valid data, continue to next
        if (!NF2FS_tree_entry_isvalid(NF2FS->ram_tree, i, father_id))
            continue;
        // return true indicate that they are equal
        if (NF2FS_ename_isequal(NF2FS, &NF2FS->ram_tree->tree_array[i], name, namelen)) {
            *index = i;
            return NF2FS_ERR_OK;
        }
    }
    // Not find tree entry in the index
    return NF2FS_ERR_TENTRY_NOFOUND;
}

// find a tree entry in the tree
int NF2FS_tree_entry_id_find(NF2FS_tree_ram_t* tree, NF2FS_size_t id, NF2FS_size_t* index)
{
    // find a free space to add the tree entry
    for (int i= 0; i < tree->entry_num; i++) {
        if (tree->tree_array[i].id == id) {
            *index = i;
            return NF2FS_ERR_OK;
        }
    }
    // Not find tree entry in the index
    return NF2FS_ERR_TENTRY_NOFOUND;
}

// update a tree entry into the tree
int NF2FS_tree_entry_update(NF2FS_tree_ram_t* tree, NF2FS_size_t id, NF2FS_size_t name_sector,
                           NF2FS_size_t name_off, NF2FS_size_t tail)
{
    NF2FS_size_t err= NF2FS_ERR_OK;
    NF2FS_size_t index= NF2FS_NULL;

    // find a free space to add the tree entry
    // if not found, return directly
    if (NF2FS_tree_entry_id_find(tree, id, &index))
        return err;

    if (name_sector != NF2FS_NULL) {
        tree->tree_array[index].name_sector= name_sector;
        tree->tree_array[index].name_off= name_off;
    }

    if (tail != NF2FS_NULL)
        tree->tree_array[index].tail_sector= tail;
    return err;
}

// remove a tree entry in the tree
int NF2FS_tree_entry_remove(NF2FS_tree_ram_t* tree, NF2FS_size_t id)
{
    NF2FS_size_t err= NF2FS_ERR_OK;
    NF2FS_size_t index= NF2FS_NULL;

    // find a free space to add the tree entry
    // if not found, return directly
    if (NF2FS_tree_entry_id_find(tree, id, &index))
        return err;

    memset(&tree->tree_array[index], 0xff, sizeof(NF2FS_tree_entry_ram_t));
    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    further tree operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Return the begin of name in a path.
// e.g., return /bin in path /usr/bin
char* NF2FS_name_in_path(char* path)
{
    // The end of the father path.
    NF2FS_size_t len = strlen(path);
    char *fend = path + len - 1;
    while (*fend != '/' && len > 0) {
        fend--;
        len--;
    }

    if (len == 0)
        fend++;

    return fend;
}

// find the father dir in flash
// if we can not find father dir in ram tree, we should use this
int NF2FS_father_dir_flash_find(NF2FS_t* NF2FS, char* path, NF2FS_size_t begin_sector, NF2FS_tree_entry_ram_t** found_entry)
{
    int err= NF2FS_ERR_OK;
    NF2FS_size_t cur_sector= begin_sector;
    NF2FS_tree_entry_ram_t* temp_entry;

    // find the end of the father dir's path
    char *name = path;
    char *father_end = NF2FS_name_in_path(name);
    while (name != father_end) {
        name += strspn(name, "/");
        NF2FS_size_t namelen= strcspn(name, "/");

        // find a free space in the tree
        // TODO in the future, if no more space, we should do something
        NF2FS_size_t entry_index= NF2FS_tree_entry_findfree(NF2FS->ram_tree);
        NF2FS_ASSERT(entry_index != NF2FS_NULL);
        temp_entry= &NF2FS->ram_tree->tree_array[entry_index];

        // traverse the dir to find the subdir
        err= NF2FS_dtraverse_name(NF2FS, cur_sector, name, namelen,
                                 NF2FS_DATA_DIR, temp_entry);
        if (err)
            return err;

        if (temp_entry->id == NF2FS_NULL) {
            // not find the entry
            // TODO in the future, we should do something
            NF2FS_ERROR("Not find the dir during path resolution");
            return NF2FS_ERR_NOFATHER;
        }

        // init for the next loop.
        cur_sector = temp_entry->tail_sector;
    }
    
    *found_entry= temp_entry;
    return NF2FS_ERR_OK;
}

// find the tree entry of path's father
int NF2FS_father_dir_find(NF2FS_t* NF2FS, char* path, NF2FS_tree_entry_ram_t** entry_addr)
{
    int err= NF2FS_ERR_OK;
    NF2FS_tree_ram_t* tree= NF2FS->ram_tree;
    NF2FS_size_t final_index= NF2FS_NULL; // record the final index of path in ram dir.

    // the end of the father path is the begin of the name in path
    char *name = path;
    char* father_end= NF2FS_name_in_path(name);

    // begin at the root path
    NF2FS_ASSERT(tree->tree_array[0].id == NF2FS_ID_ROOT);
    final_index= 0;
    while (name != father_end) {
        name += strspn(name, "/");
        NF2FS_size_t namelen= strcspn(name, "/");

        // If can not find name in the tree, break
        err= NF2FS_tree_entry_name_find(NF2FS, name, namelen, tree->tree_array[final_index].id, &final_index);
        if (err)
            break;
        name += namelen;
    }

    if (name == father_end) {
        // found father dir in the tree, return
        if (name == path) {
            // It the root path
            NF2FS_ASSERT(tree->tree_array[0].id == NF2FS_ID_ROOT);
            final_index= 0;
        }
        NF2FS_ASSERT(final_index != NF2FS_NULL);
        *entry_addr= &NF2FS->ram_tree->tree_array[final_index];
        return err;
    }

    // Can not find, should traverse NOR flash.
    err= NF2FS_father_dir_flash_find(NF2FS, name,
                                    NF2FS->ram_tree->tree_array[final_index].tail_sector,
                                    entry_addr);
    return err;
}
