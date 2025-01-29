/**
 * tree related operations.
 */
#include "N2FS_tree.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "N2FS.h"
#include "N2FS_dir.h"
#include "N2FS_rw.h"
#include "N2FS_util.h"

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ------------------------------------------------------------    Hash calculation    -----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// cal the hash value of the name, return value is the hash
N2FS_size_t N2FS_hash(uint8_t* buffer, N2FS_size_t len)
{
    N2FS_size_t hash = 5381;
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
int N2FS_tree_init(N2FS_t* N2FS, N2FS_tree_ram_t** tree_addr)
{
    int err= N2FS_ERR_OK;
    N2FS_ASSERT(N2FS->cfg->cache_size > sizeof(N2FS_tree_entry_ram_t));

    // malloc for tree
    N2FS_tree_ram_t* tree= N2FS_malloc(sizeof(N2FS_tree_ram_t));
    if (!tree) {
        err= N2FS_ERR_NOMEM;
        return err;
    }

    // malloc for tree entry
    tree->entry_num= N2FS->cfg->cache_size / sizeof(N2FS_tree_entry_ram_t);
    tree->tree_array= N2FS_malloc(N2FS->cfg->cache_size);
    if (!tree->tree_array) {
        N2FS_free(tree);
        err= N2FS_ERR_NOMEM;
        return err;
    }

    memset(tree->tree_array, 0xff, N2FS->cfg->cache_size);
    *tree_addr = tree;
    return err;
}

// find the free space for a tree entry
int N2FS_tree_entry_findfree(N2FS_tree_ram_t* tree)
{
    for (int i= 0; i < tree->entry_num; i++) {
        if (tree->tree_array[i].id == N2FS_NULL)
            return i;
    }
    return N2FS_NULL;
}

// add a tree entry into the tree
int N2FS_tree_entry_add(N2FS_tree_ram_t* tree, N2FS_size_t father_id, N2FS_size_t id,
                        N2FS_size_t name_sector, N2FS_size_t name_off, N2FS_size_t tail,
                        char* name, int namelen)
{
    N2FS_size_t err= N2FS_ERR_OK;
    N2FS_size_t index= N2FS_NULL;

    // return directly if the entry is already in tree
    for (int i= 0; i < tree->entry_num; i++) {
        if (tree->tree_array[i].id == id)
            return err;
    }

    // find a free space to add the tree entry
    index = N2FS_tree_entry_findfree(tree);

    // the tree entry is already exist in the tree
    // there is no more space in the tree
    // TODO, a replace stragety may be used in the future
    if (index == N2FS_NULL)
        return err;

    // update the tree entry message
    tree->tree_array[index].id= id;
    tree->tree_array[index].father_id= father_id;
    tree->tree_array[index].name_sector= name_sector;
    tree->tree_array[index].name_off= name_off;
    tree->tree_array[index].tail_sector= tail;
    if (namelen <= N2FS_ENTRY_NAME_LEN) {
        memcpy(tree->tree_array[index].data.name, name, namelen);
    }else {
        tree->tree_array[index].data.hash = N2FS_hash((uint8_t*)name, namelen);
    }

    return err;
}

// judge if the tree entry is valid
bool inline N2FS_tree_entry_isvalid(N2FS_tree_ram_t* tree, N2FS_size_t tree_index, N2FS_size_t father_id)
{
    return (tree->tree_array[tree_index].id != N2FS_NULL &&
            tree->tree_array[tree_index].father_id == father_id);
}

// Compare name in tree entry with name in path.
// return true if is the same
bool N2FS_ename_isequal(N2FS_t *N2FS, N2FS_tree_entry_ram_t *entry, char *name, N2FS_size_t namelen)
{
    N2FS_size_t err= N2FS_ERR_OK;
    
    // If name is in ram tree, compare directly
    if (namelen <= N2FS_ENTRY_NAME_LEN)
        return memcmp(name, entry->data.name, namelen) ? false : true;

    // If name is too long, compare hash first
    N2FS_hash_t hash = N2FS_hash((uint8_t *)name, namelen);
    if (hash == entry->data.hash) {
        // If hash is the same, compare name from NOR flash
        // TODO in the future, this may be expensive when namelen is too large
        char temp_buffer[namelen];
        err= N2FS_direct_read(N2FS, entry->name_sector, entry->name_off + sizeof(N2FS_head_t),
                              namelen, temp_buffer);
        if (err)
            return err;
        return memcmp(name, temp_buffer, namelen) ? false : true;
    }
    return false;
}

// find a tree entry in the tree with name
int N2FS_tree_entry_name_find(N2FS_t* N2FS, char* name, N2FS_size_t namelen, N2FS_size_t father_id, N2FS_size_t* index)
{
    // find a free space to add the tree entry
    for (int i= 0; i < N2FS->ram_tree->entry_num; i++) {
        // if current entry does not have valid data, continue to next
        if (!N2FS_tree_entry_isvalid(N2FS->ram_tree, i, father_id))
            continue;
        // return true indicate that they are equal
        if (N2FS_ename_isequal(N2FS, &N2FS->ram_tree->tree_array[i], name, namelen)) {
            *index = i;
            return N2FS_ERR_OK;
        }
    }
    // Not find tree entry in the index
    return N2FS_ERR_TENTRY_NOFOUND;
}

// find a tree entry in the tree
int N2FS_tree_entry_id_find(N2FS_tree_ram_t* tree, N2FS_size_t id, N2FS_size_t* index)
{
    // find a free space to add the tree entry
    for (int i= 0; i < tree->entry_num; i++) {
        if (tree->tree_array[i].id == id) {
            *index = i;
            return N2FS_ERR_OK;
        }
    }
    // Not find tree entry in the index
    return N2FS_ERR_TENTRY_NOFOUND;
}

// update a tree entry into the tree
int N2FS_tree_entry_update(N2FS_tree_ram_t* tree, N2FS_size_t id, N2FS_size_t name_sector,
                           N2FS_size_t name_off, N2FS_size_t tail)
{
    N2FS_size_t err= N2FS_ERR_OK;
    N2FS_size_t index= N2FS_NULL;

    // find a free space to add the tree entry
    // if not found, return directly
    if (N2FS_tree_entry_id_find(tree, id, &index))
        return err;

    if (name_sector != N2FS_NULL) {
        tree->tree_array[index].name_sector= name_sector;
        tree->tree_array[index].name_off= name_off;
    }

    if (tail != N2FS_NULL)
        tree->tree_array[index].tail_sector= tail;
    return err;
}

// remove a tree entry in the tree
int N2FS_tree_entry_remove(N2FS_tree_ram_t* tree, N2FS_size_t id)
{
    N2FS_size_t err= N2FS_ERR_OK;
    N2FS_size_t index= N2FS_NULL;

    // find a free space to add the tree entry
    // if not found, return directly
    if (N2FS_tree_entry_id_find(tree, id, &index))
        return err;

    memset(&tree->tree_array[index], 0xff, sizeof(N2FS_tree_entry_ram_t));
    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    further tree operations    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// Return the begin of name in a path.
// e.g., return /bin in path /usr/bin
char* N2FS_name_in_path(char* path)
{
    // The end of the father path.
    N2FS_size_t len = strlen(path);
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
int N2FS_father_dir_flash_find(N2FS_t* N2FS, char* path, N2FS_size_t begin_sector, N2FS_tree_entry_ram_t** found_entry)
{
    int err= N2FS_ERR_OK;
    N2FS_size_t cur_sector= begin_sector;
    N2FS_tree_entry_ram_t* temp_entry;

    // find the end of the father dir's path
    char *name = path;
    char *father_end = N2FS_name_in_path(name);
    while (name != father_end) {
        name += strspn(name, "/");
        N2FS_size_t namelen= strcspn(name, "/");

        // find a free space in the tree
        // TODO in the future, if no more space, we should do something
        N2FS_size_t entry_index= N2FS_tree_entry_findfree(N2FS->ram_tree);
        N2FS_ASSERT(entry_index != N2FS_NULL);
        temp_entry= &N2FS->ram_tree->tree_array[entry_index];

        // traverse the dir to find the subdir
        err= N2FS_dtraverse_name(N2FS, cur_sector, name, namelen,
                                 N2FS_DATA_DIR, temp_entry);
        if (err)
            return err;

        if (temp_entry->id == N2FS_NULL) {
            // not find the entry
            // TODO in the future, we should do something
            N2FS_ERROR("Not find the dir during path resolution");
            return N2FS_ERR_NOFATHER;
        }

        // init for the next loop.
        cur_sector = temp_entry->tail_sector;
    }
    
    *found_entry= temp_entry;
    return N2FS_ERR_OK;
}

// find the tree entry of path's father
int N2FS_father_dir_find(N2FS_t* N2FS, char* path, N2FS_tree_entry_ram_t** entry_addr)
{
    int err= N2FS_ERR_OK;
    N2FS_tree_ram_t* tree= N2FS->ram_tree;
    N2FS_size_t final_index= N2FS_NULL; // record the final index of path in ram dir.

    // the end of the father path is the begin of the name in path
    char *name = path;
    char* father_end= N2FS_name_in_path(name);

    // begin at the root path
    N2FS_ASSERT(tree->tree_array[0].id == N2FS_ID_ROOT);
    final_index= 0;
    while (name != father_end) {
        name += strspn(name, "/");
        N2FS_size_t namelen= strcspn(name, "/");

        // If can not find name in the tree, break
        err= N2FS_tree_entry_name_find(N2FS, name, namelen, tree->tree_array[final_index].id, &final_index);
        if (err)
            break;
        name += namelen;
    }

    if (name == father_end) {
        // found father dir in the tree, return
        if (name == path) {
            // It the root path
            N2FS_ASSERT(tree->tree_array[0].id == N2FS_ID_ROOT);
            final_index= 0;
        }
        N2FS_ASSERT(final_index != N2FS_NULL);
        *entry_addr= &N2FS->ram_tree->tree_array[final_index];
        return err;
    }

    // Can not find, should traverse NOR flash.
    err= N2FS_father_dir_flash_find(N2FS, name,
                                    N2FS->ram_tree->tree_array[final_index].tail_sector,
                                    entry_addr);
    return err;
}
