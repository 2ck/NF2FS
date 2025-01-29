/**
 * The basic cache operations of N2FS
 */

#include "N2FS_rw.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "N2FS.h"
#include "N2FS_head.h"
#include "N2FS_tree.h"
#include "N2FS_util.h"
#include "N2FS_manage.h"
#include "N2FS_dir.h"
#include "N2FS_file.h"

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------    Auxiliary cache functions    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

int in_place_write = 0;

void in_place_size_reset(void) 
{
    in_place_write = 0;
}

void in_place_size_print(void) 
{
    printf("The size of in place write is %d\r\n", in_place_write);
}

// Init the cache. 
int N2FS_cache_init(N2FS_t *N2FS, N2FS_cache_ram_t **cache_addr, N2FS_size_t buffer_size)
{
    int err = N2FS_ERR_OK;

    // Malloc memory for cache.
    N2FS_cache_ram_t *cache = N2FS_malloc(sizeof(N2FS_cache_ram_t));
    if (!cache) {
        err = N2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Malloc memory for cache buffer.
    cache->buffer = N2FS_malloc(buffer_size);
    if (!cache->buffer) {
        err = N2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Reset cache data.
    N2FS_cache_one(N2FS, cache);
    *cache_addr = cache;
    return err;

cleanup:
    if (cache) {
        if (cache->buffer)
            N2FS_free(cache->buffer);
        N2FS_free(cache);
    }
    return err;
}

/**
 * Empty all data in cache to avoid information leakage, i.e set all bits to 1.
 *
 * The reason to set 1 is that in nor flash, bit 1 is the origin data, and
 * a program operation can turn bit 1 to bit 0.
 */
void N2FS_cache_one(N2FS_t *N2FS, N2FS_cache_ram_t *cache)
{
    memset(cache->buffer, 0xff, N2FS->cfg->cache_size);
    cache->sector = N2FS_NULL;
    cache->off = N2FS_NULL;
    cache->size= 0;
    cache->change_flag = false;
}

// reset data in the buffer, may used in sync function
void N2FS_cache_buffer_reset(N2FS_t *N2FS, N2FS_cache_ram_t *cache)
{
    memset(cache->buffer, 0xff, N2FS->cfg->cache_size);
    cache->change_flag = false;
}

/**
 * Drop the message in cache, a cheaper way than N2FS_cache_one.
 */
void N2FS_cache_drop(N2FS_t *N2FS, N2FS_cache_ram_t *cache)
{
    (void)N2FS;
    cache->sector = N2FS_NULL;
    cache->off = N2FS_NULL;
    cache->size= 0;
    cache->change_flag = false;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------    Prog/Erase cache functions    ------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * Set all writen flag of data head to 0 to confirm that it's writen without corrupt,
 * and the writen mode of data in buffer is forward.
 */
int N2FS_cache_writen_flag(N2FS_t *N2FS, N2FS_size_t off, N2FS_size_t size, uint8_t* buffer, bool if_flush, N2FS_size_t flush_sector)
{
    int err= N2FS_ERR_OK;
    N2FS_ssize_t rest_size = size;
    uint8_t *data = buffer;
    N2FS_head_t head;
    N2FS_size_t len;
    while (rest_size > 0) {
        head = *(N2FS_head_t *)data;
        if (rest_size == size && off == 0) {
            // it is sector head
            switch (N2FS_shead_type(head)) {
            case N2FS_SECTOR_SUPER:
                len= N2FS_min(sizeof(N2FS_super_sector_flash_t), rest_size);
                break;

            case N2FS_SECTOR_DIR:
                len= N2FS_min(sizeof(N2FS_dir_sector_flash_t), rest_size);
                break;

            case N2FS_SECTOR_BFILE:
                len= N2FS_min(sizeof(N2FS_bfile_sector_flash_t), rest_size);
                break;
            
            default:
                N2FS_ERROR("sector header in cache is wrong\r\n");
                break;
            }
        } else if (head == N2FS_NULL || rest_size < sizeof(N2FS_head_t)) {
            // the data is not entirely in cache
            return N2FS_ERR_OK;
        } else {
            // the right logic
            len = N2FS_dhead_dsize(head);
            *(N2FS_head_t *)data &= N2FS_DHEAD_WRITTEN_SET;
            head &= N2FS_DHEAD_WRITTEN_SET;
            if (if_flush) {
                in_place_write += sizeof(N2FS_head_t);
                err= N2FS->cfg->prog(N2FS->cfg, flush_sector, off, &head, sizeof(N2FS_head_t));
                if (err)
                    return err;
            }

            // data is not entirely in cache, indicating that the loop is over
            if (rest_size < len)
                return N2FS_ERR_OK;
        }

        data += len;
        rest_size -= len;
        off += len;
    }

    // wrong because the rest_size is not 0
    if (rest_size != 0) {
        N2FS_ERROR("err is in N2FS_cache_writen_flag\r\n");
        return N2FS_ERR_WRONGCAL;
    }
    return N2FS_ERR_OK;
}

/**
 * Program(write) data in pcache to nor flash.
 *
 * If validata is valid, we should read what we have writen and compare to
 * data in pcache to make sure it's absolutely right.
 *
 * When flushing, we should turn writen bit of data head to 0, the flag tells us
 * the written mode in buffer, it's forward or backward.
 */
int N2FS_cache_flush(N2FS_t *N2FS, N2FS_cache_ram_t *pcache)
{
    int err = N2FS_ERR_OK;
    if (pcache->sector == N2FS_NULL || !pcache->change_flag) {
        // if cache don't have data or do not change, return directly
        return N2FS_ERR_OK;
    }

    // Program data into nor flash.
    N2FS_ASSERT(pcache->sector < N2FS->cfg->sector_count);
    err = N2FS->cfg->prog(N2FS->cfg, pcache->sector,
                            pcache->off, pcache->buffer, pcache->size);
    if (err) {
        return err;
    }

    // set the written flag in pcache to 0
    err = N2FS_cache_writen_flag(N2FS, pcache->off, pcache->size, pcache->buffer, true, pcache->sector);
    if (err)
        return err;

    // // prog the written flag again
    // err = N2FS->cfg->prog(N2FS->cfg, pcache->sector, pcache->off,
    //                       pcache->buffer, pcache->size);
    // if (err)
    //     return err;

    // sync data in rcache
    N2FS_dprog_cache_sync(N2FS, N2FS->rcache, pcache->sector, pcache->off, pcache->size,
                          pcache->buffer, N2FS_DPROG_CACHE_DATA_PROG, false);

    N2FS_cache_one(N2FS, pcache);
    pcache->change_flag = false;
    return err;
}

// read data with cache
int N2FS_cache_read(N2FS_t* N2FS, const N2FS_cache_ram_t* pcache, N2FS_cache_ram_t* rcache,
                    N2FS_size_t sector, N2FS_off_t off, void *buffer, N2FS_size_t size)
{
    int err = N2FS_ERR_OK;
    uint8_t *data = (uint8_t *)buffer;

    // Checkout whether or not (sector, off, size) is right.
    if (sector >= N2FS->cfg->sector_count ||
        off + size > N2FS->cfg->sector_size)
    {
        return N2FS_ERR_WRONGCAL;
    }

    N2FS_size_t rest_size = size;
    while (rest_size > 0)
    {
        // diff is the size of data memcpy function should copy.
        N2FS_size_t diff = rest_size;

        // Find data in pcache first.
        if (pcache && sector == pcache->sector && off < pcache->off + pcache->size) {
            // If the start of the read data is already in pcache, we should read them.
            if (off >= pcache->off) {
                diff = N2FS_min(diff, pcache->size - (off - pcache->off));
                memcpy(data, &pcache->buffer[off - pcache->off], diff);

                data += diff;
                off += diff;
                rest_size -= diff;
                continue;
            }

            // If the start of the read data is before pcache, we should read them first.
            diff = N2FS_min(diff, pcache->off - off);
        }

        // Find data in read cache second, similar to above.
        if (sector == rcache->sector && off < rcache->off + rcache->size) {
            if (off >= rcache->off){
                diff = N2FS_min(diff, rcache->size - (off - rcache->off));
                memcpy(data, &rcache->buffer[off - rcache->off], diff);

                data += diff;
                off += diff;
                rest_size -= diff;
                continue;
            }

            diff = N2FS_min(diff, rcache->off - off);
        }

        // Read data to buffer directly.
        int err = N2FS_direct_read(N2FS, sector, off, diff, data);
        if (err)
        {
            return err;
        }

        data += diff;
        off += diff;
        rest_size -= diff;
        continue;
    }

    return err;
}

// prog data with cache
int N2FS_cache_prog(N2FS_t* N2FS, N2FS_cache_ram_t* pcache, N2FS_cache_ram_t* rcache,
                    N2FS_size_t sector, N2FS_off_t off, void *buffer, N2FS_size_t size)
{
    int err = N2FS_ERR_OK;
    const uint8_t* data= (uint8_t*)buffer;

    // basic check
    N2FS_ASSERT(sector < N2FS->cfg->sector_count);
    N2FS_ASSERT(size <= N2FS->cfg->cache_size);
    N2FS_ASSERT(off + size <= N2FS->cfg->sector_size);

    N2FS_size_t rest_size = size;
    while (rest_size > 0) {
        // If the rest data can prog to the current cache
        if (sector == pcache->sector && off >= pcache->off + pcache->size &&
            off + size < pcache->off + N2FS->cfg->cache_size){
            // We think it's append write, not random write.
            N2FS_size_t diff = N2FS_min(N2FS->cfg->cache_size - pcache->size,rest_size);
            memcpy(&pcache->buffer[pcache->size], data, diff);

            // sync pcache data to rcache
            N2FS_dprog_cache_sync(N2FS, N2FS->rcache, sector, off, diff,
                                  (uint8_t*)data, N2FS_DPROG_CACHE_DATA_PROG, true);
                                  
            // update message
            data += diff;
            off += diff;
            rest_size -= diff;

            // If pcache is full, then flush.
            pcache->size+= diff;
            pcache->change_flag = true;
            if (pcache->size > N2FS->cfg->cache_size - sizeof(N2FS_head_t)) {
                err = N2FS_cache_flush(N2FS, pcache);
                if (err) {
                    return err;
                }
            }
            continue;
        }

        // Make sure pcache is not used by any other sectors when we use it,
        // i.e we have flushed all data in pcache.
        if (pcache->sector != N2FS_NULL) {
            err = N2FS_cache_flush(N2FS, pcache);
            if (err) {
                return err;
            }
            N2FS_cache_one(N2FS, N2FS->pcache);
        }

        // prepare pcache for the next use.
        pcache->sector = sector;
        pcache->off = off;
        pcache->size = 0;
    }

    return err;
}

/**
 * Read data to cache.
 * Sometime N2FS_cache_read function doesn't work well, so we do this.
 */
int N2FS_read_to_cache(N2FS_t* N2FS, N2FS_cache_ram_t* cache, N2FS_size_t sector,
                       N2FS_off_t off, N2FS_size_t size)
{
    int err = N2FS_ERR_OK;

    N2FS_ASSERT(off + size <= N2FS->cfg->sector_size);
    if ((cache->sector == sector) && (cache->off == off) && (cache->size == size)) {
        // data are just in the cache
        return err;
    } else if ((sector == N2FS->pcache->sector) && (off + size > N2FS->pcache->off)
                && (off < N2FS->pcache->off + N2FS->pcache->size)) {
        // there has some data in pcache and they has not flush to flash
        if (off == N2FS->pcache->off) {
            // data that need is entirely in pcache
            memcpy(cache->buffer, N2FS->pcache->buffer, N2FS->pcache->size);
            memset((uint8_t *)cache->buffer + N2FS->pcache->size, 0xff, N2FS->cfg->cache_size - N2FS->pcache->size);
            err= N2FS_cache_writen_flag(N2FS, N2FS->pcache->off, N2FS->pcache->size, cache->buffer, false, N2FS_NULL);
            if (err)
                return err;
        } else if (off < N2FS->pcache->off && N2FS->pcache->off - off > sizeof(N2FS_head_t)) {
            // still has some data in flash, the front pcache has valid data
            N2FS_size_t temp_size= N2FS->pcache->off - off;
            err= N2FS_direct_read(N2FS, sector, off, temp_size, cache->buffer);
            if (err)
                return err;
            
            // the other data are in pcache
            N2FS_ASSERT(size - temp_size > 0);
            memcpy(cache->buffer + temp_size, N2FS->pcache->buffer, size - temp_size);
            err= N2FS_cache_writen_flag(N2FS, N2FS->pcache->off, size - temp_size, cache->buffer + temp_size, false, N2FS_NULL);
            if (err)
                return err;
        } else {
            N2FS_size_t temp_size= off - N2FS->pcache->off;
            N2FS_size_t copy_cache_size= N2FS->pcache->size - temp_size;
            if (copy_cache_size < sizeof(N2FS_head_t)) {
                // no more valid data in pcache, read directly
                err= N2FS_direct_read(N2FS, sector, off, size, cache->buffer);
                if (err)
                    return err;
            } else {
                // all data are part of pcache data, if data is not in pcache, it hasn't been written
                memcpy(cache->buffer, N2FS->pcache->buffer + temp_size, copy_cache_size);
                N2FS_ASSERT(copy_cache_size + off <= N2FS->cfg->sector_size);
                err= N2FS_direct_read(N2FS, sector, off + copy_cache_size, size - copy_cache_size, cache->buffer + copy_cache_size);
                if (err)
                    return err;
                err= N2FS_cache_writen_flag(N2FS, off, N2FS->pcache->size - off + N2FS->pcache->off, cache->buffer, false, N2FS_NULL);
                if (err)
                    return err;
            }
        }

        cache->sector= sector;
        cache->off= off;
        cache->size= size;
        return err;
    }

    N2FS_cache_one(N2FS, cache);
    cache->sector = sector;
    cache->off = off;
    cache->size = size;
    err= N2FS_direct_read(N2FS, cache->sector, cache->off,
                          cache->size, cache->buffer);
    N2FS_ASSERT(err <= 0);
    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * --------------------------------------------------------    Prog/Erase without cache    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// directly read data without buffer
int N2FS_direct_read(N2FS_t* N2FS, N2FS_size_t sector, N2FS_off_t off, N2FS_size_t size, void* buffer)
{
    int err= N2FS_ERR_OK;
    err= N2FS->cfg->read(N2FS->cfg, sector, off, buffer, size);
    return err;
}

// when prog directly, we should sync cache message
// return indicates that whether cache data has changed
void N2FS_dprog_cache_sync(N2FS_t* N2FS, N2FS_cache_ram_t* cache, N2FS_size_t sector,
                            N2FS_off_t off, N2FS_size_t size, void* buffer, int dp_type, bool if_written_flag)
{
    if (sector == cache->sector && off + size > cache->off && off < cache->off + cache->size) {
        uint8_t* temp_data= (uint8_t*)cache->buffer + off - cache->off;
        if (dp_type == N2FS_DPROG_CACHE_HEAD_CHANGE && off >= cache->off && 
            off < cache->off + cache->size) {
            // if update message is just head, use this
            N2FS_ASSERT(size == sizeof(N2FS_head_t));
            *(N2FS_head_t*)temp_data&= (*(N2FS_head_t*)buffer);
        } else if (dp_type == N2FS_DPROG_CACHE_DATA_PROG) {

            if (off < cache->off) {
                // the front part of data in buffer is not in cache
                N2FS_size_t temp_size= cache->off - off;
                N2FS_ASSERT(size - temp_size <= N2FS->cfg->cache_size);
                memcpy(cache->buffer, ((uint8_t *)(buffer) + temp_size), size - temp_size);
                cache->size= N2FS_max(cache->size, size - temp_size);

                N2FS_size_t err= N2FS_cache_writen_flag(N2FS, cache->off, size - temp_size, cache->buffer, false, N2FS_NULL);
                if (err)
                    N2FS_ASSERT(-1 > 0);
            } else {
                // the front part of data in buffer is in cache
                int temp_size= N2FS_min(size, N2FS->cfg->cache_size - (off - cache->off));
                memcpy(temp_data, buffer, temp_size);

                N2FS_size_t err= N2FS_cache_writen_flag(N2FS, off, temp_size, temp_data, false, N2FS_NULL);
                if (err)
                    N2FS_ASSERT(-1 > 0);

                // change the cache size, change_flag don't need to set to true
                cache->size = N2FS_max(off - cache->off + temp_size, cache->size);
            }
        }
    }
}

// directly validate data, but should sync buffer message
int N2FS_head_validate(N2FS_t* N2FS, N2FS_size_t sector, N2FS_size_t off, N2FS_head_t head_flag)
{
    int err= N2FS_ERR_OK;
    in_place_write += sizeof(N2FS_head_t);
    err= N2FS->cfg->prog(N2FS->cfg, sector, off, &head_flag, sizeof(N2FS_head_t));
    N2FS_dprog_cache_sync(N2FS, N2FS->pcache, sector, off, sizeof(N2FS_head_t),
                          &head_flag, N2FS_DPROG_CACHE_HEAD_CHANGE, false);
    N2FS_dprog_cache_sync(N2FS, N2FS->rcache, sector, off, sizeof(N2FS_head_t),
                          &head_flag, N2FS_DPROG_CACHE_HEAD_CHANGE, false);
    return err;
}

/**
 * Directily prog a sector head or a data to flash
 */
int N2FS_direct_prog(N2FS_t* N2FS, N2FS_size_t data_type, N2FS_size_t sector,
                     N2FS_off_t off, N2FS_size_t size, void* buffer)
{
    int err = N2FS_ERR_OK;
    uint8_t* data= (uint8_t*)buffer;
    N2FS_ASSERT(sector < N2FS->cfg->sector_count && off + size <= N2FS->cfg->sector_size);

    // prog data first
    err = N2FS->cfg->prog(N2FS->cfg, sector, off, data, size);
    N2FS_ASSERT(err <= 0);
    if (err)
    {
        return err;
    }

    // if we prog data, written flag in data head should be validated
    if (data_type == N2FS_DIRECT_PROG_DHEAD) {
        in_place_write += sizeof(N2FS_head_t);
        N2FS_head_t *head = (N2FS_head_t *)buffer;
        *head &= N2FS_DHEAD_WRITTEN_SET;
        err = N2FS->cfg->prog(N2FS->cfg, sector, off, data, size);
        N2FS_ASSERT(err <= 0);
    }

    // prog data shold also sync changed in pcache and rcache
    N2FS_dprog_cache_sync(N2FS, N2FS->pcache, sector, off, size, buffer,
                          N2FS_DPROG_CACHE_DATA_PROG, false);
    N2FS_dprog_cache_sync(N2FS, N2FS->rcache, sector, off, size, buffer,
                          N2FS_DPROG_CACHE_DATA_PROG, false);

    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------    More complex operations    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * Prog address of map/hash tree/wl to superblock directly.
 */
int N2FS_prog_in_superblock(N2FS_t* N2FS, N2FS_superblock_ram_t* super, void* buffer,
                            N2FS_size_t size)
{
    int err = N2FS_ERR_OK;

    if (super->free_off + size > N2FS->cfg->sector_size) {
        // If current super block is full, choose the other one.
        err = N2FS_superblock_change(N2FS, super, N2FS->pcache, false);
        if (err)
            return err;
    }

    // prog new wl message address to superblock directly.
    err= N2FS_direct_prog(N2FS, N2FS_DIRECT_PROG_DHEAD,
                          super->sector, super->free_off, size, buffer);
    if (err)
        return err;

    super->free_off+= size;
    return err;
}

/**
 * Prog all metadata into a new superblock when init fs or change superbleok.
 */
int N2FS_superblock_change(N2FS_t *N2FS, N2FS_superblock_ram_t *super,
                           N2FS_cache_ram_t *pcache, bool if_commit)
{
    int err= N2FS_ERR_OK;

    err= N2FS_cache_flush(N2FS, pcache);
    if (err)
        return err;

    // Change the using superblock to the other
    super->sector = (super->sector + 1) % 2;
    super->free_off = 0;
    N2FS_head_t head;
    if (!N2FS_sector_erase(N2FS, super->sector, &head)) {
        N2FS_ERROR("Fail to erase superblock\n");
        return N2FS_ERR_INVAL;
    }

    // Prog basic sector head message.
    N2FS_head_t new_head= N2FS_MKSHEAD(0, N2FS_STATE_ALLOCATING, N2FS_SECTOR_SUPER,
                                       (N2FS_shead_extend(head) + 2)%0x40, N2FS_shead_etimes(head) + 1);
    err= N2FS_direct_prog(N2FS, N2FS_DIRECT_PROG_SHEAD, super->sector,
                          super->free_off, sizeof(N2FS_head_t), &new_head);
    super->free_off += sizeof(N2FS_head_t);
    if (err)
        return err;

    // 0. init the pcache for programing
    N2FS_cache_flush(N2FS, pcache);
    pcache->sector = super->sector;
    pcache->off= super->free_off;
    pcache->size= 0;
    pcache->change_flag= true;

    // 1. Prog Super message, 36B
    N2FS_supermessage_flash_t* prog1= (N2FS_supermessage_flash_t*)pcache->buffer;
    N2FS_size_t len= sizeof(N2FS_supermessage_flash_t);
    N2FS_ASSERT(len < N2FS->cfg->cache_size);
    prog1->head= N2FS_MKDHEAD(0, 1, N2FS_ID_SUPER, N2FS_DATA_SUPER_MESSAGE, len);
    memcpy(prog1->fs_name, &N2FS_FS_NAME, sizeof(N2FS_FS_NAME));
    prog1->version= N2FS_VERSION;
    prog1->sector_size= N2FS->cfg->sector_size;
    prog1->sector_count= N2FS->cfg->sector_count;
    prog1->name_max= N2FS_min(N2FS->cfg->name_max, N2FS_NAME_MAX);
    prog1->file_max= N2FS_min(N2FS->cfg->file_max, N2FS_FILE_MAX_SIZE);
    prog1->region_cnt= N2FS->cfg->region_cnt;

    super->free_off+= len;
    pcache->size+= len;
    prog1 = (N2FS_supermessage_flash_t*)((uint8_t*)prog1 + len);

    // 2. Region map, store two region map, 36B
    N2FS_region_map_flash_t* prog2= NULL;
    N2FS_size_t map_len = N2FS_alignup(N2FS->cfg->region_cnt, 8) / 8;
    len= sizeof(N2FS_region_map_flash_t) + 2 * map_len;
    N2FS_ASSERT(len < N2FS->cfg->cache_size);
    if (pcache->size + len > N2FS->cfg->cache_size) {
        N2FS_cache_flush(N2FS, pcache);
        pcache->off= super->free_off;
        pcache->size= 0;
        pcache->change_flag= true;
        prog2 = (N2FS_region_map_flash_t*)pcache->buffer;
    } else {
        prog2 = (N2FS_region_map_flash_t*)prog1;
    }
    prog2->head= N2FS_MKDHEAD(0, 1, N2FS_ID_SUPER, N2FS_DATA_REGION_MAP, len);
    memcpy((uint8_t*)prog2->map, N2FS->manager->region_map->dir_region, map_len);
    memcpy((uint8_t*)prog2->map + map_len, N2FS->manager->region_map->bfile_region, map_len);
    N2FS->manager->region_map->begin= super->sector;
    N2FS->manager->region_map->off= super->free_off;

    super->free_off+= len;
    pcache->size= pcache->size + len;
    prog2= (N2FS_region_map_flash_t*)((uint8_t*)prog2 + len);

    // 3. prog ID map, 16B
    N2FS_mapaddr_flash_t* prog3= NULL;
    N2FS_size_t sector_num= N2FS_alignup(2 * N2FS_ID_MAX / 8, N2FS->cfg->sector_size) /
                            N2FS->cfg->sector_size;
    N2FS_ASSERT(sector_num == 1);
    len= sizeof(N2FS_mapaddr_flash_t) + sector_num * sizeof(N2FS_size_t);
    N2FS_ASSERT(len < N2FS->cfg->cache_size);
    if (pcache->size + len > N2FS->cfg->cache_size) {
        N2FS_cache_flush(N2FS, pcache);
        pcache->size= 0;
        pcache->off= super->free_off;
        pcache->change_flag= true;
        prog3 = (N2FS_mapaddr_flash_t*)pcache->buffer;
    } else {
        prog3 = (N2FS_mapaddr_flash_t*)prog2;
    }
    prog3->head= N2FS_MKDHEAD(0, 1, N2FS_ID_SUPER, N2FS_DATA_ID_MAP, len);
    prog3->begin= N2FS->id_map->begin;
    prog3->off= N2FS->id_map->off;
    prog3->erase_times[0] = N2FS->id_map->etimes;

    super->free_off+= len;
    pcache->size= pcache->size + len;
    prog3= (N2FS_mapaddr_flash_t*)((uint8_t*)prog3 + len);

    // 4. prog sector map, 16B
    N2FS_mapaddr_flash_t* prog4= NULL;
    sector_num= N2FS_alignup(2 * N2FS->cfg->sector_count / 8, N2FS->cfg->sector_size) /
                N2FS->cfg->sector_size;
    len= sizeof(N2FS_mapaddr_flash_t) + sector_num * sizeof(N2FS_size_t);
    N2FS_ASSERT(len < N2FS->cfg->cache_size);
    if (pcache->size + len > N2FS->cfg->cache_size) {
        N2FS_cache_flush(N2FS, pcache);
        pcache->size= 0;
        pcache->off= super->free_off;
        pcache->change_flag= true;
        prog4 = (N2FS_mapaddr_flash_t*)pcache->buffer;
    } else {
        prog4 = (N2FS_mapaddr_flash_t*)prog3;
    }
    prog4->head= N2FS_MKDHEAD(0, 1, N2FS_ID_SUPER, N2FS_DATA_SECTOR_MAP, len);
    prog4->begin= N2FS->manager->smap_begin;
    prog4->off= N2FS->manager->smap_off;
    for (int i= 0; i < sector_num; i++) {
        prog4->erase_times[i] = N2FS->manager->etimes[i];
    }

    super->free_off+= len;
    pcache->size= pcache->size + len;
    prog4= (N2FS_mapaddr_flash_t*)((uint8_t*)prog4 + len);

    // 5. prog the root dir address, 8B
    N2FS_dir_name_flash_t* prog5= NULL;
    len= sizeof(N2FS_dir_name_flash_t);
    N2FS_ASSERT(len < N2FS->cfg->cache_size);
    if (pcache->size + len > N2FS->cfg->cache_size) {
        N2FS_cache_flush(N2FS, pcache);
        pcache->size= 0;
        pcache->off= super->free_off;
        pcache->change_flag= true;
        prog5 = (N2FS_dir_name_flash_t*)pcache->buffer;
    } else {
        prog5 = (N2FS_dir_name_flash_t*)prog4;
    }
    prog5->head= N2FS_MKDHEAD(0, 1, N2FS_ID_ROOT, N2FS_DATA_DIR_NAME, len);
    prog5->tail= N2FS->ram_tree->tree_array[0].tail_sector; 
    N2FS_ASSERT(N2FS->ram_tree->tree_array[0].id == N2FS_ID_ROOT);
    // root dir do not need to record name

    // update in-ram root tree entry
    err= N2FS_tree_entry_update(N2FS->ram_tree, N2FS_ID_ROOT, super->sector,
                                super->free_off, N2FS_NULL);
    if (err)
        return err;

    // update in-ram open dir
    N2FS_dir_ram_t* root_dir= NULL;
    err= N2FS_open_dir_find(N2FS, N2FS_ID_ROOT, &root_dir);
    if (err)
        return err;
    root_dir->name_sector= super->sector;
    root_dir->name_off= super->free_off;
    root_dir->namelen= 0;

    super->free_off+= len;
    pcache->size= pcache->size + len;
    prog5= (N2FS_dir_name_flash_t*)((uint8_t*)prog5 + len);

    // 6. prog the address of wl message
    N2FS_wladdr_flash_t* prog6= NULL;
    if (N2FS->manager->scan_times >= N2FS_WL_START) {
        len= sizeof(N2FS_wladdr_flash_t);
        if (pcache->size + len > N2FS->cfg->cache_size) {
            N2FS_cache_flush(N2FS, pcache);
            pcache->size= 0;
            pcache->off= super->free_off;
            pcache->change_flag= true;
            prog6 = (N2FS_wladdr_flash_t*)pcache->buffer;
        } else {
            prog6 = (N2FS_wladdr_flash_t*)prog5;
        }
        prog6->head= N2FS_MKDHEAD(0, 1, N2FS_ID_SUPER, N2FS_DATA_WL_ADDR, len);
        prog6->begin= N2FS->manager->wl->begin;

        super->free_off+= len;
        pcache->size= pcache->size + len;
        prog6= (N2FS_wladdr_flash_t*)((uint8_t*)prog6 + len);
    }else {
        prog6= (N2FS_wladdr_flash_t*)prog5;
    }

    // 7. Commit message, 24B
    if (if_commit) {
        N2FS_commit_flash_t* prog7= NULL;
        len= sizeof(N2FS_commit_flash_t);
        N2FS_ASSERT(len < N2FS->cfg->cache_size);
        if (pcache->size + len > N2FS->cfg->cache_size) {
            N2FS_cache_flush(N2FS, pcache);
            pcache->size= 0;
            pcache->off= super->free_off;
            pcache->change_flag= true;
            prog7 = (N2FS_commit_flash_t*)pcache->buffer;
        } else {
            prog7 = (N2FS_commit_flash_t*)prog6;
        }
        prog7->head= N2FS_MKDHEAD(0, 1, N2FS_ID_SUPER, N2FS_DATA_COMMIT, len);
        prog7->next_id= N2FS->id_map->free_map->index_or_changed +
                        N2FS->id_map->free_map->region * N2FS->id_map->ids_in_buffer;
        prog7->scan_times= N2FS->manager->scan_times;
        prog7->next_dir_sector= N2FS->manager->dir_map->index_or_changed +
                                N2FS->manager->dir_map->region * N2FS->manager->region_size;
        prog7->next_bfile_sector= N2FS->manager->bfile_map->index_or_changed +
                                N2FS->manager->bfile_map->region * N2FS->manager->region_size;
        prog7->reserve_region= N2FS->manager->region_map->reserve;

        super->free_off+= len;
        pcache->size= pcache->size + len;
        prog7= (N2FS_commit_flash_t*)((uint8_t*)prog7 + len);
    }

    // All data has proged, validate the sector head
    N2FS_cache_flush(N2FS, N2FS->pcache);
    err= N2FS_head_validate(N2FS, N2FS->superblock->sector, 0,
                            N2FS_SHEAD_USING_SET);
    return err;
}

// find valid sectors in the region, write the bitmap to buffer
// note that buffer size should alignup to sizeof(uint32_t)
int N2FS_find_sectors_in_region(N2FS_t* N2FS, N2FS_size_t region, uint32_t* buffer)
{
    int err = N2FS_ERR_OK;

    // Read dir map.
    N2FS_size_t size= N2FS_alignup(N2FS->manager->region_size / 8,
                                   sizeof(uint32_t));
    N2FS_size_t sector = N2FS->manager->smap_begin;
    N2FS_size_t off = N2FS->manager->smap_off + region * size;
    while (off >= N2FS->cfg->sector_size) {
        sector++;
        off -= N2FS->cfg->sector_size;
    }

    err = N2FS_direct_read(N2FS, sector, off, size, buffer);
    N2FS_ASSERT(err <= 0);
    if (err)
        return err;

    // Read erase map.
    uint32_t temp_buffer[size / sizeof(uint32_t)];
    sector = N2FS->manager->smap_begin;
    off = N2FS->manager->smap_off + region * size + N2FS->cfg->sector_count / 8;
    while (off >= N2FS->cfg->sector_size) {
        sector++;
        off -= N2FS->cfg->sector_size;
    }

    err = N2FS_direct_read(N2FS, sector, off, size, temp_buffer);
    N2FS_ASSERT(err <= 0);
    if (err)
        return err;

    // Merge data in two maps.
    size = size / 4;
    for (int i = 0; i < size; i++) {
        buffer[i] |= ~temp_buffer[i];
    }
    return err;
}

// Calculate the number of valid bits(0) in cache buffer.
N2FS_size_t N2FS_cal_valid_bits(N2FS_cache_ram_t* cache)
{
    N2FS_ASSERT(cache->size % sizeof(uint32_t) == 0);
    uint32_t* data= (uint32_t*)cache->buffer;
    N2FS_size_t num = 0;

    // loop times of i and j
    N2FS_size_t i_index= cache->size / sizeof(N2FS_size_t);
    N2FS_size_t j_index= sizeof(N2FS_size_t) * 8;
    for (int i= 0; i < i_index; i++) {
        for (int j= 0; j < j_index; j++) {
            if (((data[i] >> j) & 1U) == 0)
                num++;
        }
    }
    return num;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------    Delete/erase operations    ------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// delete data and add the old_space for father
int N2FS_data_delete(N2FS_t* N2FS, N2FS_size_t father_id, N2FS_size_t sector,
                     N2FS_off_t off, N2FS_size_t size)
{
    int err = N2FS_ERR_OK;

    // If sector is N2FS_NULL, it means that there is no data in flash.
    if (sector == N2FS_NULL)
        return err;

    // set head to delete type
    N2FS_head_validate(N2FS, sector, off, N2FS_DHEAD_DELETE_SET);

    // We don't need to calculate old space of super message.
    if (father_id == N2FS_ID_SUPER)
        return err;

    // Find file's father dir.
    N2FS_dir_ram_t *father_dir = N2FS->dir_list;
    while (father_dir != NULL && father_dir->id != father_id) {
        father_dir = father_dir->next_dir;
    }

    // Not found.
    if (father_dir == NULL) {
        N2FS_ERROR("The father dir of the file is not opened");
        return N2FS_ERR_NOFATHER;
    }

    // add the old_space for the father
    father_dir->old_space += size;
    return err;
}

// set shead to delete type, change the remove bitmap
int N2FS_sequen_sector_old(N2FS_t* N2FS, N2FS_size_t begin, N2FS_size_t num)
{
    int err= N2FS_ERR_OK;

    // not valid sector, return directly
    if (begin == N2FS_NULL)
        return err;

    // set the sector head to old.
    N2FS_size_t sector = begin;
    for (int i= 0; i < num; i++) {
        err= N2FS_head_validate(N2FS, sector, 0, N2FS_SHEAD_OLD_SET);
        if (err)
            return err;
        sector++;
    }

    // Turn bits in erase map to 0, so it can reuse in the future.
    err = N2FS_emap_set(N2FS, N2FS->manager, begin, num);
    return err;
}

// similar to N2FS_sequen_sector_old, but should traverse indexs to sectors
int N2FS_bfile_sector_old(N2FS_t* N2FS, N2FS_bfile_index_ram_t* index, N2FS_size_t num)
{
    int err = N2FS_ERR_OK;

    for (int i = 0; i < num; i++) {
        if ((index[i].sector == N2FS_NULL) && !(i == 0 || i == num - 1) ) {
            N2FS_ERROR("the index is wrong in N2FS_bfile_sector_old");
            return N2FS_ERR_IO;
        }

        // Loop for num of indexes.
        N2FS_size_t off = index[i].off;
        N2FS_size_t rest_size = index[i].size;

        N2FS_size_t cnt = 0;
        while (rest_size > 0) {
            // Calculate the number of sequential sectors the index has.
            N2FS_size_t size = N2FS_min(N2FS->cfg->sector_size - off, rest_size);
            cnt++;
            rest_size -= size;
            off += size;
            if (off == N2FS->cfg->sector_size)
                off = sizeof(N2FS_bfile_sector_flash_t);
        }

        // Set all these sequential sectors to old.
        err = N2FS_sequen_sector_old(N2FS, index[i].sector, cnt);
        if (err)
            return err;
    }
    return err;
}

// erase a normal sector, should return the origin head to help building a new shead
// return true if we truly erase it.
bool N2FS_sector_erase(N2FS_t* N2FS, N2FS_size_t sector, N2FS_head_t* head)
{
    int err= N2FS_ERR_OK;

    // Read sector head of the sector.
    N2FS_ASSERT(sector < N2FS->cfg->sector_count);
    err= N2FS_direct_read(N2FS, sector, 0, sizeof(N2FS_head_t), head);
    N2FS_ASSERT(err == N2FS_ERR_OK);

    // we do not need to erase if it has no data
    if (*head == N2FS_NULL)
        return true;

    // Erase it if current sector has data.
    // When a id map sector has beed freed, it only records the etimes but not N2FS_NULL
    if (N2FS_shead_check(*head, N2FS_STATE_FREE, N2FS_SECTOR_NOTSURE)) {
        err = N2FS->cfg->erase(N2FS->cfg, sector);
        N2FS_ASSERT(err == N2FS_ERR_OK);
        
        // set the bit in erae map to 0
        err= N2FS_emap_set(N2FS, N2FS->manager, sector, 1);
        N2FS_ASSERT(err == N2FS_ERR_OK);

        return true;
    }

    return false;
}

// erase sectors belonged to id/sector map without shead
int N2FS_map_sector_erase(N2FS_t* N2FS, N2FS_size_t begin, N2FS_size_t num, N2FS_size_t* etimes)
{
    int err = N2FS_ERR_OK;

    N2FS_head_t head;
    for (int i = 0; i < num; i++) {
        // Erase old one directly.
        err= N2FS->cfg->erase(N2FS->cfg, begin);
        if (err)
            return err;

        // Prog new sector head, valid flag 0 is used for N2FS_shead_check
        // to ensure that the sector is free to use with etimes recording
        head = N2FS_MKSHEAD(0, N2FS_STATE_FREE, N2FS_SECTOR_NOTSURE, 0x3f, etimes[i] + 1);
        err= N2FS_direct_prog(N2FS, N2FS_DIRECT_PROG_SHEAD, begin, 0,
                              sizeof(N2FS_head_t), &head);
        if (err)
            return err;

        begin++;
    }
    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * --------------------------------------------------------    GC & WL operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// migrate the two regions with reserve region
int N2FS_region_migration(N2FS_t* N2FS, N2FS_size_t region_1, N2FS_size_t region_2)
{
    // TODO in the future, currently wl is not finished
    return N2FS_ERR_OK;
}

// GC for a dir
int N2FS_dir_gc(N2FS_t* N2FS, N2FS_dir_ram_t* dir)
{
    int err = N2FS_ERR_OK;
    N2FS_size_t old_tail= dir->tail_sector;

    // flush data to flash first.
    err= N2FS_cache_flush(N2FS, N2FS->pcache);
    if (err)
        return err;

    // set son file's old index/data to delete
    N2FS_file_ram_t* file= N2FS->file_list;
    while (file != NULL) {
        if (file->father_id == dir->id) {
            err= N2FS_data_delete(N2FS, dir->id, file->file_cache.sector, file->file_cache.off,
                                  N2FS_dhead_dsize(*(N2FS_head_t *)file->file_cache.buffer));
            if (err)
                return err;
        }
        file= file->next_file;
    }

    // Starting gc.
    err = N2FS_dtraverse_gc(N2FS, dir);
    if (err)
        return err;

    // flush opened son file to flash
    file= N2FS->file_list;
    while (file != NULL) {
        if (file->father_id == dir->id) {
            // prog new data/index to flash
            N2FS_head_t old_head = *(N2FS_head_t *)file->file_cache.buffer;
            N2FS_head_t *head = (N2FS_head_t *)file->file_cache.buffer;
            *head= N2FS_MKDHEAD(0, 1, file->id, N2FS_dhead_type(old_head), file->file_cache.size);
            err= N2FS_dir_prog(N2FS, dir, file->file_cache.buffer, file->file_cache.size);
            if (err)
                return err;

            // Update file cache message.
            file->file_cache.sector = dir->tail_sector;
            file->file_cache.off = dir->tail_off - file->file_cache.size;
            file->file_cache.change_flag = false;
        }
        file= file->next_file;
    }

    // recycle the old dir sectors
    err = N2FS_dir_old(N2FS, old_tail);
    if (err)
        return err;

    // update dir message in its father dir.
    err = N2FS_dir_update(N2FS, dir);
    return err;
}

// GC for big file
int N2FS_bfile_gc(N2FS_t *N2FS, N2FS_file_ram_t *file)
{
    int err = N2FS_ERR_OK;

    N2FS_bfile_index_flash_t *bfile_index = (N2FS_bfile_index_flash_t *)file->file_cache.buffer;
    N2FS_size_t num = (file->file_cache.size - sizeof(N2FS_size_t)) /
                        sizeof(N2FS_bfile_index_ram_t);

    // No need to do gc
    N2FS_ASSERT(num < N2FS_FILE_INDEX_MAX);
    if (num < N2FS_FILE_INDEX_NUM)
        return err;

    // find candidate indexes that can be gc
    N2FS_size_t candidate_arr[N2FS_FILE_INDEX_NUM] = {0};
    N2FS_size_t arr_len = 0;
    for (int i = 0; i < num; i++) {
        if (bfile_index->index[i].size <= N2FS->cfg->sector_size) {
            candidate_arr[arr_len] = i;
            arr_len++;
        }
    }
    N2FS_ASSERT(arr_len <= N2FS_FILE_INDEX_NUM);

    N2FS_size_t gc_size = 0;
    N2FS_size_t min, max;
    N2FS_size_t distance = 0;
    for (int i = 0; i < arr_len; i++) {
        for (int j= i + 1; j < arr_len; j++) {
            // cal size that can be gc to merge these candidate indexes
            gc_size = 0;
            for (int k = candidate_arr[i]; k <= candidate_arr[j]; k++)
                gc_size += bfile_index->index[k].size;

            // loop to get the max distance, which means gc can reduce most indexes
            if (gc_size < N2FS->manager->region_size * N2FS->cfg->sector_size) {
                if (candidate_arr[j] - candidate_arr[i] > distance) {
                    min = candidate_arr[i];
                    max = candidate_arr[j];
                    distance = max - min;
                }
            }
        }
    }

    // TODO in the future, distiance 0 means we can not gc now
    if (distance == 0)
        return err;

    // cal the gc_size and gc_num
    N2FS_size_t len = N2FS->cfg->sector_size - sizeof(N2FS_bfile_sector_flash_t);
    gc_size = 0;
    for (int i = min; i <= max; i++)
        gc_size += bfile_index->index[i].size;
    N2FS_size_t gc_num = N2FS_alignup(gc_size, len) / len;

    // TODO
    printf("GC size is %d\r\n", gc_size);

    // gc for part of big file.
    err = N2FS_bfile_part_gc(N2FS, file, min, max, gc_size, num, gc_num);
    if (err) {
        N2FS_ERROR("N2FS_bfile_part_gc WRONG!\n");
        return err;
    }
    return err;
}
