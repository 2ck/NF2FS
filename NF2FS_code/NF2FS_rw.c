/**
 * The basic cache operations of NF2FS
 */

#include "NF2FS_rw.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "NF2FS.h"
#include "NF2FS_head.h"
#include "NF2FS_tree.h"
#include "NF2FS_util.h"
#include "NF2FS_manage.h"
#include "NF2FS_dir.h"
#include "NF2FS_file.h"

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
int NF2FS_cache_init(NF2FS_t *NF2FS, NF2FS_cache_ram_t **cache_addr, NF2FS_size_t buffer_size)
{
    int err = NF2FS_ERR_OK;

    // Malloc memory for cache.
    NF2FS_cache_ram_t *cache = NF2FS_malloc(sizeof(NF2FS_cache_ram_t));
    if (!cache) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Malloc memory for cache buffer.
    cache->buffer = NF2FS_malloc(buffer_size);
    if (!cache->buffer) {
        err = NF2FS_ERR_NOMEM;
        goto cleanup;
    }

    // Reset cache data.
    NF2FS_cache_one(NF2FS, cache);
    *cache_addr = cache;
    return err;

cleanup:
    if (cache) {
        if (cache->buffer)
            NF2FS_free(cache->buffer);
        NF2FS_free(cache);
    }
    return err;
}

/**
 * Empty all data in cache to avoid information leakage, i.e set all bits to 1.
 *
 * The reason to set 1 is that in nor flash, bit 1 is the origin data, and
 * a program operation can turn bit 1 to bit 0.
 */
void NF2FS_cache_one(NF2FS_t *NF2FS, NF2FS_cache_ram_t *cache)
{
    memset(cache->buffer, 0xff, NF2FS->cfg->cache_size);
    cache->sector = NF2FS_NULL;
    cache->off = NF2FS_NULL;
    cache->size= 0;
    cache->change_flag = false;
}

// reset data in the buffer, may used in sync function
void NF2FS_cache_buffer_reset(NF2FS_t *NF2FS, NF2FS_cache_ram_t *cache)
{
    memset(cache->buffer, 0xff, NF2FS->cfg->cache_size);
    cache->change_flag = false;
}

/**
 * Drop the message in cache, a cheaper way than NF2FS_cache_one.
 */
void NF2FS_cache_drop(NF2FS_t *NF2FS, NF2FS_cache_ram_t *cache)
{
    (void)NF2FS;
    cache->sector = NF2FS_NULL;
    cache->off = NF2FS_NULL;
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
int NF2FS_cache_writen_flag(NF2FS_t *NF2FS, NF2FS_size_t off, NF2FS_size_t size, uint8_t* buffer, bool if_flush, NF2FS_size_t flush_sector)
{
    int err= NF2FS_ERR_OK;
    NF2FS_ssize_t rest_size = size;
    uint8_t *data = buffer;
    NF2FS_head_t head;
    NF2FS_size_t len;
    while (rest_size > 0) {
        head = *(NF2FS_head_t *)data;
        if (rest_size == size && off == 0) {
            // it is sector head
            switch (NF2FS_shead_type(head)) {
            case NF2FS_SECTOR_SUPER:
                len= NF2FS_min(sizeof(NF2FS_super_sector_flash_t), rest_size);
                break;

            case NF2FS_SECTOR_DIR:
                len= NF2FS_min(sizeof(NF2FS_dir_sector_flash_t), rest_size);
                break;

            case NF2FS_SECTOR_BFILE:
                len= NF2FS_min(sizeof(NF2FS_bfile_sector_flash_t), rest_size);
                break;
            
            default:
                NF2FS_ERROR("sector header in cache is wrong\r\n");
                break;
            }
        } else if (head == NF2FS_NULL || rest_size < sizeof(NF2FS_head_t)) {
            // the data is not entirely in cache
            return NF2FS_ERR_OK;
        } else {
            // the right logic
            len = NF2FS_dhead_dsize(head);
            *(NF2FS_head_t *)data &= NF2FS_DHEAD_WRITTEN_SET;
            head &= NF2FS_DHEAD_WRITTEN_SET;
            if (if_flush) {
                in_place_write += sizeof(NF2FS_head_t);
                err= NF2FS->cfg->prog(NF2FS->cfg, flush_sector, off, &head, sizeof(NF2FS_head_t));
                if (err)
                    return err;
            }

            // data is not entirely in cache, indicating that the loop is over
            if (rest_size < len)
                return NF2FS_ERR_OK;
        }

        data += len;
        rest_size -= len;
        off += len;
    }

    // wrong because the rest_size is not 0
    if (rest_size != 0) {
        NF2FS_ERROR("err is in NF2FS_cache_writen_flag\r\n");
        return NF2FS_ERR_WRONGCAL;
    }
    return NF2FS_ERR_OK;
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
int NF2FS_cache_flush(NF2FS_t *NF2FS, NF2FS_cache_ram_t *pcache)
{
    int err = NF2FS_ERR_OK;
    if (pcache->sector == NF2FS_NULL || !pcache->change_flag) {
        // if cache don't have data or do not change, return directly
        return NF2FS_ERR_OK;
    }

    // Program data into nor flash.
    NF2FS_ASSERT(pcache->sector < NF2FS->cfg->sector_count);
    err = NF2FS->cfg->prog(NF2FS->cfg, pcache->sector,
                            pcache->off, pcache->buffer, pcache->size);
    if (err) {
        return err;
    }

    // set the written flag in pcache to 0
    err = NF2FS_cache_writen_flag(NF2FS, pcache->off, pcache->size, pcache->buffer, true, pcache->sector);
    if (err)
        return err;

    // // prog the written flag again
    // err = NF2FS->cfg->prog(NF2FS->cfg, pcache->sector, pcache->off,
    //                       pcache->buffer, pcache->size);
    // if (err)
    //     return err;

    // sync data in rcache
    NF2FS_dprog_cache_sync(NF2FS, NF2FS->rcache, pcache->sector, pcache->off, pcache->size,
                          pcache->buffer, NF2FS_DPROG_CACHE_DATA_PROG, false);

    NF2FS_cache_one(NF2FS, pcache);
    pcache->change_flag = false;
    return err;
}

// read data with cache
int NF2FS_cache_read(NF2FS_t* NF2FS, const NF2FS_cache_ram_t* pcache, NF2FS_cache_ram_t* rcache,
                    NF2FS_size_t sector, NF2FS_off_t off, void *buffer, NF2FS_size_t size)
{
    int err = NF2FS_ERR_OK;
    uint8_t *data = (uint8_t *)buffer;

    // Checkout whether or not (sector, off, size) is right.
    if (sector >= NF2FS->cfg->sector_count ||
        off + size > NF2FS->cfg->sector_size)
    {
        return NF2FS_ERR_WRONGCAL;
    }

    NF2FS_size_t rest_size = size;
    while (rest_size > 0)
    {
        // diff is the size of data memcpy function should copy.
        NF2FS_size_t diff = rest_size;

        // Find data in pcache first.
        if (pcache && sector == pcache->sector && off < pcache->off + pcache->size) {
            // If the start of the read data is already in pcache, we should read them.
            if (off >= pcache->off) {
                diff = NF2FS_min(diff, pcache->size - (off - pcache->off));
                memcpy(data, &pcache->buffer[off - pcache->off], diff);

                data += diff;
                off += diff;
                rest_size -= diff;
                continue;
            }

            // If the start of the read data is before pcache, we should read them first.
            diff = NF2FS_min(diff, pcache->off - off);
        }

        // Find data in read cache second, similar to above.
        if (sector == rcache->sector && off < rcache->off + rcache->size) {
            if (off >= rcache->off){
                diff = NF2FS_min(diff, rcache->size - (off - rcache->off));
                memcpy(data, &rcache->buffer[off - rcache->off], diff);

                data += diff;
                off += diff;
                rest_size -= diff;
                continue;
            }

            diff = NF2FS_min(diff, rcache->off - off);
        }

        // Read data to buffer directly.
        int err = NF2FS_direct_read(NF2FS, sector, off, diff, data);
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
int NF2FS_cache_prog(NF2FS_t* NF2FS, NF2FS_cache_ram_t* pcache, NF2FS_cache_ram_t* rcache,
                    NF2FS_size_t sector, NF2FS_off_t off, void *buffer, NF2FS_size_t size)
{
    int err = NF2FS_ERR_OK;
    const uint8_t* data= (uint8_t*)buffer;

    // basic check
    NF2FS_ASSERT(sector < NF2FS->cfg->sector_count);
    NF2FS_ASSERT(size <= NF2FS->cfg->cache_size);
    NF2FS_ASSERT(off + size <= NF2FS->cfg->sector_size);

    NF2FS_size_t rest_size = size;
    while (rest_size > 0) {
        // If the rest data can prog to the current cache
        if (sector == pcache->sector && off >= pcache->off + pcache->size &&
            off + size < pcache->off + NF2FS->cfg->cache_size){
            // We think it's append write, not random write.
            NF2FS_size_t diff = NF2FS_min(NF2FS->cfg->cache_size - pcache->size,rest_size);
            memcpy(&pcache->buffer[pcache->size], data, diff);

            // sync pcache data to rcache
            NF2FS_dprog_cache_sync(NF2FS, NF2FS->rcache, sector, off, diff,
                                  (uint8_t*)data, NF2FS_DPROG_CACHE_DATA_PROG, true);
                                  
            // update message
            data += diff;
            off += diff;
            rest_size -= diff;

            // If pcache is full, then flush.
            pcache->size+= diff;
            pcache->change_flag = true;
            if (pcache->size > NF2FS->cfg->cache_size - sizeof(NF2FS_head_t)) {
                err = NF2FS_cache_flush(NF2FS, pcache);
                if (err) {
                    return err;
                }
            }
            continue;
        }

        // Make sure pcache is not used by any other sectors when we use it,
        // i.e we have flushed all data in pcache.
        if (pcache->sector != NF2FS_NULL) {
            err = NF2FS_cache_flush(NF2FS, pcache);
            if (err) {
                return err;
            }
            NF2FS_cache_one(NF2FS, NF2FS->pcache);
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
 * Sometime NF2FS_cache_read function doesn't work well, so we do this.
 */
int NF2FS_read_to_cache(NF2FS_t* NF2FS, NF2FS_cache_ram_t* cache, NF2FS_size_t sector,
                       NF2FS_off_t off, NF2FS_size_t size)
{
    int err = NF2FS_ERR_OK;

    NF2FS_ASSERT(off + size <= NF2FS->cfg->sector_size);
    if ((cache->sector == sector) && (cache->off == off) && (cache->size == size)) {
        // data are just in the cache
        return err;
    } else if ((sector == NF2FS->pcache->sector) && (off + size > NF2FS->pcache->off)
                && (off < NF2FS->pcache->off + NF2FS->pcache->size)) {
        // there has some data in pcache and they has not flush to flash
        if (off == NF2FS->pcache->off) {
            // data that need is entirely in pcache
            memcpy(cache->buffer, NF2FS->pcache->buffer, NF2FS->pcache->size);
            memset((uint8_t *)cache->buffer + NF2FS->pcache->size, 0xff, NF2FS->cfg->cache_size - NF2FS->pcache->size);
            err= NF2FS_cache_writen_flag(NF2FS, NF2FS->pcache->off, NF2FS->pcache->size, cache->buffer, false, NF2FS_NULL);
            if (err)
                return err;
        } else if (off < NF2FS->pcache->off && NF2FS->pcache->off - off > sizeof(NF2FS_head_t)) {
            // still has some data in flash, the front pcache has valid data
            NF2FS_size_t temp_size= NF2FS->pcache->off - off;
            err= NF2FS_direct_read(NF2FS, sector, off, temp_size, cache->buffer);
            if (err)
                return err;
            
            // the other data are in pcache
            NF2FS_ASSERT(size - temp_size > 0);
            memcpy(cache->buffer + temp_size, NF2FS->pcache->buffer, size - temp_size);
            err= NF2FS_cache_writen_flag(NF2FS, NF2FS->pcache->off, size - temp_size, cache->buffer + temp_size, false, NF2FS_NULL);
            if (err)
                return err;
        } else {
            NF2FS_size_t temp_size= off - NF2FS->pcache->off;
            NF2FS_size_t copy_cache_size= NF2FS->pcache->size - temp_size;
            if (copy_cache_size < sizeof(NF2FS_head_t)) {
                // no more valid data in pcache, read directly
                err= NF2FS_direct_read(NF2FS, sector, off, size, cache->buffer);
                if (err)
                    return err;
            } else {
                // all data are part of pcache data, if data is not in pcache, it hasn't been written
                memcpy(cache->buffer, NF2FS->pcache->buffer + temp_size, copy_cache_size);
                NF2FS_ASSERT(copy_cache_size + off <= NF2FS->cfg->sector_size);
                err= NF2FS_direct_read(NF2FS, sector, off + copy_cache_size, size - copy_cache_size, cache->buffer + copy_cache_size);
                if (err)
                    return err;
                err= NF2FS_cache_writen_flag(NF2FS, off, NF2FS->pcache->size - off + NF2FS->pcache->off, cache->buffer, false, NF2FS_NULL);
                if (err)
                    return err;
            }
        }

        cache->sector= sector;
        cache->off= off;
        cache->size= size;
        return err;
    }

    NF2FS_cache_one(NF2FS, cache);
    cache->sector = sector;
    cache->off = off;
    cache->size = size;
    err= NF2FS_direct_read(NF2FS, cache->sector, cache->off,
                          cache->size, cache->buffer);
    NF2FS_ASSERT(err <= 0);
    return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * --------------------------------------------------------    Prog/Erase without cache    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// directly read data without buffer
int NF2FS_direct_read(NF2FS_t* NF2FS, NF2FS_size_t sector, NF2FS_off_t off, NF2FS_size_t size, void* buffer)
{
    int err= NF2FS_ERR_OK;
    err= NF2FS->cfg->read(NF2FS->cfg, sector, off, buffer, size);
    return err;
}

// when prog directly, we should sync cache message
// return indicates that whether cache data has changed
void NF2FS_dprog_cache_sync(NF2FS_t* NF2FS, NF2FS_cache_ram_t* cache, NF2FS_size_t sector,
                            NF2FS_off_t off, NF2FS_size_t size, void* buffer, int dp_type, bool if_written_flag)
{
    if (sector == cache->sector && off + size > cache->off && off < cache->off + cache->size) {
        uint8_t* temp_data= (uint8_t*)cache->buffer + off - cache->off;
        if (dp_type == NF2FS_DPROG_CACHE_HEAD_CHANGE && off >= cache->off && 
            off < cache->off + cache->size) {
            // if update message is just head, use this
            NF2FS_ASSERT(size == sizeof(NF2FS_head_t));
            *(NF2FS_head_t*)temp_data&= (*(NF2FS_head_t*)buffer);
        } else if (dp_type == NF2FS_DPROG_CACHE_DATA_PROG) {

            if (off < cache->off) {
                // the front part of data in buffer is not in cache
                NF2FS_size_t temp_size= cache->off - off;
                NF2FS_ASSERT(size - temp_size <= NF2FS->cfg->cache_size);
                memcpy(cache->buffer, ((uint8_t *)(buffer) + temp_size), size - temp_size);
                cache->size= NF2FS_max(cache->size, size - temp_size);

                NF2FS_size_t err= NF2FS_cache_writen_flag(NF2FS, cache->off, size - temp_size, cache->buffer, false, NF2FS_NULL);
                if (err)
                    NF2FS_ASSERT(-1 > 0);
            } else {
                // the front part of data in buffer is in cache
                int temp_size= NF2FS_min(size, NF2FS->cfg->cache_size - (off - cache->off));
                memcpy(temp_data, buffer, temp_size);

                NF2FS_size_t err= NF2FS_cache_writen_flag(NF2FS, off, temp_size, temp_data, false, NF2FS_NULL);
                if (err)
                    NF2FS_ASSERT(-1 > 0);

                // change the cache size, change_flag don't need to set to true
                cache->size = NF2FS_max(off - cache->off + temp_size, cache->size);
            }
        }
    }
}

// directly validate data, but should sync buffer message
int NF2FS_head_validate(NF2FS_t* NF2FS, NF2FS_size_t sector, NF2FS_size_t off, NF2FS_head_t head_flag)
{
    int err= NF2FS_ERR_OK;
    in_place_write += sizeof(NF2FS_head_t);
    err= NF2FS->cfg->prog(NF2FS->cfg, sector, off, &head_flag, sizeof(NF2FS_head_t));
    NF2FS_dprog_cache_sync(NF2FS, NF2FS->pcache, sector, off, sizeof(NF2FS_head_t),
                          &head_flag, NF2FS_DPROG_CACHE_HEAD_CHANGE, false);
    NF2FS_dprog_cache_sync(NF2FS, NF2FS->rcache, sector, off, sizeof(NF2FS_head_t),
                          &head_flag, NF2FS_DPROG_CACHE_HEAD_CHANGE, false);
    return err;
}

/**
 * Directily prog a sector head or a data to flash
 */
int NF2FS_direct_prog(NF2FS_t* NF2FS, NF2FS_size_t data_type, NF2FS_size_t sector,
                     NF2FS_off_t off, NF2FS_size_t size, void* buffer)
{
    int err = NF2FS_ERR_OK;
    uint8_t* data= (uint8_t*)buffer;
    NF2FS_ASSERT(sector < NF2FS->cfg->sector_count && off + size <= NF2FS->cfg->sector_size);

    // prog data first
    err = NF2FS->cfg->prog(NF2FS->cfg, sector, off, data, size);
    NF2FS_ASSERT(err <= 0);
    if (err)
    {
        return err;
    }

    // if we prog data, written flag in data head should be validated
    if (data_type == NF2FS_DIRECT_PROG_DHEAD) {
        in_place_write += sizeof(NF2FS_head_t);
        NF2FS_head_t *head = (NF2FS_head_t *)buffer;
        *head &= NF2FS_DHEAD_WRITTEN_SET;
        err = NF2FS->cfg->prog(NF2FS->cfg, sector, off, data, size);
        NF2FS_ASSERT(err <= 0);
    }

    // prog data shold also sync changed in pcache and rcache
    NF2FS_dprog_cache_sync(NF2FS, NF2FS->pcache, sector, off, size, buffer,
                          NF2FS_DPROG_CACHE_DATA_PROG, false);
    NF2FS_dprog_cache_sync(NF2FS, NF2FS->rcache, sector, off, size, buffer,
                          NF2FS_DPROG_CACHE_DATA_PROG, false);

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
int NF2FS_prog_in_superblock(NF2FS_t* NF2FS, NF2FS_superblock_ram_t* super, void* buffer,
                            NF2FS_size_t size)
{
    int err = NF2FS_ERR_OK;

    if (super->free_off + size > NF2FS->cfg->sector_size) {
        // If current super block is full, choose the other one.
        err = NF2FS_superblock_change(NF2FS, super, NF2FS->pcache, false);
        if (err)
            return err;
    }

    // prog new wl message address to superblock directly.
    err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_DHEAD,
                          super->sector, super->free_off, size, buffer);
    if (err)
        return err;

    super->free_off+= size;
    return err;
}

/**
 * Prog all metadata into a new superblock when init fs or change superbleok.
 */
int NF2FS_superblock_change(NF2FS_t *NF2FS, NF2FS_superblock_ram_t *super,
                           NF2FS_cache_ram_t *pcache, bool if_commit)
{
    int err= NF2FS_ERR_OK;

    err= NF2FS_cache_flush(NF2FS, pcache);
    if (err)
        return err;

    // Change the using superblock to the other
    super->sector = (super->sector + 1) % 2;
    super->free_off = 0;
    NF2FS_head_t head;
    if (!NF2FS_sector_erase(NF2FS, super->sector, &head)) {
        NF2FS_ERROR("Fail to erase superblock\n");
        return NF2FS_ERR_INVAL;
    }

    // Prog basic sector head message.
    NF2FS_head_t new_head= NF2FS_MKSHEAD(0, NF2FS_STATE_ALLOCATING, NF2FS_SECTOR_SUPER,
                                       (NF2FS_shead_extend(head) + 2)%0x40, NF2FS_shead_etimes(head) + 1);
    err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_SHEAD, super->sector,
                          super->free_off, sizeof(NF2FS_head_t), &new_head);
    super->free_off += sizeof(NF2FS_head_t);
    if (err)
        return err;

    // 0. init the pcache for programing
    NF2FS_cache_flush(NF2FS, pcache);
    pcache->sector = super->sector;
    pcache->off= super->free_off;
    pcache->size= 0;
    pcache->change_flag= true;

    // 1. Prog Super message, 36B
    NF2FS_supermessage_flash_t* prog1= (NF2FS_supermessage_flash_t*)pcache->buffer;
    NF2FS_size_t len= sizeof(NF2FS_supermessage_flash_t);
    NF2FS_ASSERT(len < NF2FS->cfg->cache_size);
    prog1->head= NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_SUPER_MESSAGE, len);
    memcpy(prog1->fs_name, &NF2FS_FS_NAME, sizeof(NF2FS_FS_NAME));
    prog1->version= NF2FS_VERSION;
    prog1->sector_size= NF2FS->cfg->sector_size;
    prog1->sector_count= NF2FS->cfg->sector_count;
    prog1->name_max= NF2FS_min(NF2FS->cfg->name_max, NF2FS_NAME_MAX);
    prog1->file_max= NF2FS_min(NF2FS->cfg->file_max, NF2FS_FILE_MAX_SIZE);
    prog1->region_cnt= NF2FS->cfg->region_cnt;

    super->free_off+= len;
    pcache->size+= len;
    prog1 = (NF2FS_supermessage_flash_t*)((uint8_t*)prog1 + len);

    // 2. Region map, store two region map, 36B
    NF2FS_region_map_flash_t* prog2= NULL;
    NF2FS_size_t map_len = NF2FS_alignup(NF2FS->cfg->region_cnt, 8) / 8;
    len= sizeof(NF2FS_region_map_flash_t) + 2 * map_len;
    NF2FS_ASSERT(len < NF2FS->cfg->cache_size);
    if (pcache->size + len > NF2FS->cfg->cache_size) {
        NF2FS_cache_flush(NF2FS, pcache);
        pcache->off= super->free_off;
        pcache->size= 0;
        pcache->change_flag= true;
        prog2 = (NF2FS_region_map_flash_t*)pcache->buffer;
    } else {
        prog2 = (NF2FS_region_map_flash_t*)prog1;
    }
    prog2->head= NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_REGION_MAP, len);
    memcpy((uint8_t*)prog2->map, NF2FS->manager->region_map->dir_region, map_len);
    memcpy((uint8_t*)prog2->map + map_len, NF2FS->manager->region_map->bfile_region, map_len);
    NF2FS->manager->region_map->begin= super->sector;
    NF2FS->manager->region_map->off= super->free_off;

    super->free_off+= len;
    pcache->size= pcache->size + len;
    prog2= (NF2FS_region_map_flash_t*)((uint8_t*)prog2 + len);

    // 3. prog ID map, 16B
    NF2FS_mapaddr_flash_t* prog3= NULL;
    NF2FS_size_t sector_num= NF2FS_alignup(2 * NF2FS_ID_MAX / 8, NF2FS->cfg->sector_size) /
                            NF2FS->cfg->sector_size;
    NF2FS_ASSERT(sector_num == 1);
    len= sizeof(NF2FS_mapaddr_flash_t) + sector_num * sizeof(NF2FS_size_t);
    NF2FS_ASSERT(len < NF2FS->cfg->cache_size);
    if (pcache->size + len > NF2FS->cfg->cache_size) {
        NF2FS_cache_flush(NF2FS, pcache);
        pcache->size= 0;
        pcache->off= super->free_off;
        pcache->change_flag= true;
        prog3 = (NF2FS_mapaddr_flash_t*)pcache->buffer;
    } else {
        prog3 = (NF2FS_mapaddr_flash_t*)prog2;
    }
    prog3->head= NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_ID_MAP, len);
    prog3->begin= NF2FS->id_map->begin;
    prog3->off= NF2FS->id_map->off;
    prog3->erase_times[0] = NF2FS->id_map->etimes;

    super->free_off+= len;
    pcache->size= pcache->size + len;
    prog3= (NF2FS_mapaddr_flash_t*)((uint8_t*)prog3 + len);

    // 4. prog sector map, 16B
    NF2FS_mapaddr_flash_t* prog4= NULL;
    sector_num= NF2FS_alignup(2 * NF2FS->cfg->sector_count / 8, NF2FS->cfg->sector_size) /
                NF2FS->cfg->sector_size;
    len= sizeof(NF2FS_mapaddr_flash_t) + sector_num * sizeof(NF2FS_size_t);
    NF2FS_ASSERT(len < NF2FS->cfg->cache_size);
    if (pcache->size + len > NF2FS->cfg->cache_size) {
        NF2FS_cache_flush(NF2FS, pcache);
        pcache->size= 0;
        pcache->off= super->free_off;
        pcache->change_flag= true;
        prog4 = (NF2FS_mapaddr_flash_t*)pcache->buffer;
    } else {
        prog4 = (NF2FS_mapaddr_flash_t*)prog3;
    }
    prog4->head= NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_SECTOR_MAP, len);
    prog4->begin= NF2FS->manager->smap_begin;
    prog4->off= NF2FS->manager->smap_off;
    for (int i= 0; i < sector_num; i++) {
        prog4->erase_times[i] = NF2FS->manager->etimes[i];
    }

    super->free_off+= len;
    pcache->size= pcache->size + len;
    prog4= (NF2FS_mapaddr_flash_t*)((uint8_t*)prog4 + len);

    // 5. prog the root dir address, 8B
    NF2FS_dir_name_flash_t* prog5= NULL;
    len= sizeof(NF2FS_dir_name_flash_t);
    NF2FS_ASSERT(len < NF2FS->cfg->cache_size);
    if (pcache->size + len > NF2FS->cfg->cache_size) {
        NF2FS_cache_flush(NF2FS, pcache);
        pcache->size= 0;
        pcache->off= super->free_off;
        pcache->change_flag= true;
        prog5 = (NF2FS_dir_name_flash_t*)pcache->buffer;
    } else {
        prog5 = (NF2FS_dir_name_flash_t*)prog4;
    }
    prog5->head= NF2FS_MKDHEAD(0, 1, NF2FS_ID_ROOT, NF2FS_DATA_DIR_NAME, len);
    prog5->tail= NF2FS->ram_tree->tree_array[0].tail_sector; 
    NF2FS_ASSERT(NF2FS->ram_tree->tree_array[0].id == NF2FS_ID_ROOT);
    // root dir do not need to record name

    // update in-ram root tree entry
    err= NF2FS_tree_entry_update(NF2FS->ram_tree, NF2FS_ID_ROOT, super->sector,
                                super->free_off, NF2FS_NULL);
    if (err)
        return err;

    // update in-ram open dir
    NF2FS_dir_ram_t* root_dir= NULL;
    err= NF2FS_open_dir_find(NF2FS, NF2FS_ID_ROOT, &root_dir);
    if (err)
        return err;
    root_dir->name_sector= super->sector;
    root_dir->name_off= super->free_off;
    root_dir->namelen= 0;

    super->free_off+= len;
    pcache->size= pcache->size + len;
    prog5= (NF2FS_dir_name_flash_t*)((uint8_t*)prog5 + len);

    // 6. prog the address of wl message
    NF2FS_wladdr_flash_t* prog6= NULL;
    if (NF2FS->manager->scan_times >= NF2FS_WL_START) {
        len= sizeof(NF2FS_wladdr_flash_t);
        if (pcache->size + len > NF2FS->cfg->cache_size) {
            NF2FS_cache_flush(NF2FS, pcache);
            pcache->size= 0;
            pcache->off= super->free_off;
            pcache->change_flag= true;
            prog6 = (NF2FS_wladdr_flash_t*)pcache->buffer;
        } else {
            prog6 = (NF2FS_wladdr_flash_t*)prog5;
        }
        prog6->head= NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_WL_ADDR, len);
        prog6->begin= NF2FS->manager->wl->begin;

        super->free_off+= len;
        pcache->size= pcache->size + len;
        prog6= (NF2FS_wladdr_flash_t*)((uint8_t*)prog6 + len);
    }else {
        prog6= (NF2FS_wladdr_flash_t*)prog5;
    }

    // 7. Commit message, 24B
    if (if_commit) {
        NF2FS_commit_flash_t* prog7= NULL;
        len= sizeof(NF2FS_commit_flash_t);
        NF2FS_ASSERT(len < NF2FS->cfg->cache_size);
        if (pcache->size + len > NF2FS->cfg->cache_size) {
            NF2FS_cache_flush(NF2FS, pcache);
            pcache->size= 0;
            pcache->off= super->free_off;
            pcache->change_flag= true;
            prog7 = (NF2FS_commit_flash_t*)pcache->buffer;
        } else {
            prog7 = (NF2FS_commit_flash_t*)prog6;
        }
        prog7->head= NF2FS_MKDHEAD(0, 1, NF2FS_ID_SUPER, NF2FS_DATA_COMMIT, len);
        prog7->next_id= NF2FS->id_map->free_map->index_or_changed +
                        NF2FS->id_map->free_map->region * NF2FS->id_map->ids_in_buffer;
        prog7->scan_times= NF2FS->manager->scan_times;
        prog7->next_dir_sector= NF2FS->manager->dir_map->index_or_changed +
                                NF2FS->manager->dir_map->region * NF2FS->manager->region_size;
        prog7->next_bfile_sector= NF2FS->manager->bfile_map->index_or_changed +
                                NF2FS->manager->bfile_map->region * NF2FS->manager->region_size;
        prog7->reserve_region= NF2FS->manager->region_map->reserve;

        super->free_off+= len;
        pcache->size= pcache->size + len;
        prog7= (NF2FS_commit_flash_t*)((uint8_t*)prog7 + len);
    }

    // All data has proged, validate the sector head
    NF2FS_cache_flush(NF2FS, NF2FS->pcache);
    err= NF2FS_head_validate(NF2FS, NF2FS->superblock->sector, 0,
                            NF2FS_SHEAD_USING_SET);
    return err;
}

// find valid sectors in the region, write the bitmap to buffer
// note that buffer size should alignup to sizeof(uint32_t)
int NF2FS_find_sectors_in_region(NF2FS_t* NF2FS, NF2FS_size_t region, uint32_t* buffer)
{
    int err = NF2FS_ERR_OK;

    // Read dir map.
    NF2FS_size_t size= NF2FS_alignup(NF2FS->manager->region_size / 8,
                                   sizeof(uint32_t));
    NF2FS_size_t sector = NF2FS->manager->smap_begin;
    NF2FS_size_t off = NF2FS->manager->smap_off + region * size;
    while (off >= NF2FS->cfg->sector_size) {
        sector++;
        off -= NF2FS->cfg->sector_size;
    }

    err = NF2FS_direct_read(NF2FS, sector, off, size, buffer);
    NF2FS_ASSERT(err <= 0);
    if (err)
        return err;

    // Read erase map.
    uint32_t temp_buffer[size / sizeof(uint32_t)];
    sector = NF2FS->manager->smap_begin;
    off = NF2FS->manager->smap_off + region * size + NF2FS->cfg->sector_count / 8;
    while (off >= NF2FS->cfg->sector_size) {
        sector++;
        off -= NF2FS->cfg->sector_size;
    }

    err = NF2FS_direct_read(NF2FS, sector, off, size, temp_buffer);
    NF2FS_ASSERT(err <= 0);
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
NF2FS_size_t NF2FS_cal_valid_bits(NF2FS_cache_ram_t* cache)
{
    NF2FS_ASSERT(cache->size % sizeof(uint32_t) == 0);
    uint32_t* data= (uint32_t*)cache->buffer;
    NF2FS_size_t num = 0;

    // loop times of i and j
    NF2FS_size_t i_index= cache->size / sizeof(NF2FS_size_t);
    NF2FS_size_t j_index= sizeof(NF2FS_size_t) * 8;
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
int NF2FS_data_delete(NF2FS_t* NF2FS, NF2FS_size_t father_id, NF2FS_size_t sector,
                     NF2FS_off_t off, NF2FS_size_t size)
{
    int err = NF2FS_ERR_OK;

    // If sector is NF2FS_NULL, it means that there is no data in flash.
    if (sector == NF2FS_NULL)
        return err;

    // set head to delete type
    NF2FS_head_validate(NF2FS, sector, off, NF2FS_DHEAD_DELETE_SET);

    // We don't need to calculate old space of super message.
    if (father_id == NF2FS_ID_SUPER)
        return err;

    // Find file's father dir.
    NF2FS_dir_ram_t *father_dir = NF2FS->dir_list;
    while (father_dir != NULL && father_dir->id != father_id) {
        father_dir = father_dir->next_dir;
    }

    // Not found.
    if (father_dir == NULL) {
        NF2FS_ERROR("The father dir of the file is not opened");
        return NF2FS_ERR_NOFATHER;
    }

    // add the old_space for the father
    father_dir->old_space += size;
    return err;
}

// set shead to delete type, change the remove bitmap
int NF2FS_sequen_sector_old(NF2FS_t* NF2FS, NF2FS_size_t begin, NF2FS_size_t num)
{
    int err= NF2FS_ERR_OK;

    // not valid sector, return directly
    if (begin == NF2FS_NULL)
        return err;

    // set the sector head to old.
    NF2FS_size_t sector = begin;
    for (int i= 0; i < num; i++) {
        err= NF2FS_head_validate(NF2FS, sector, 0, NF2FS_SHEAD_OLD_SET);
        if (err)
            return err;
        sector++;
    }

    // Turn bits in erase map to 0, so it can reuse in the future.
    err = NF2FS_emap_set(NF2FS, NF2FS->manager, begin, num);
    return err;
}

// similar to NF2FS_sequen_sector_old, but should traverse indexs to sectors
int NF2FS_bfile_sector_old(NF2FS_t* NF2FS, NF2FS_bfile_index_ram_t* index, NF2FS_size_t num)
{
    int err = NF2FS_ERR_OK;

    for (int i = 0; i < num; i++) {
        if ((index[i].sector == NF2FS_NULL) && !(i == 0 || i == num - 1) ) {
            NF2FS_ERROR("the index is wrong in NF2FS_bfile_sector_old");
            return NF2FS_ERR_IO;
        }

        // Loop for num of indexes.
        NF2FS_size_t off = index[i].off;
        NF2FS_size_t rest_size = index[i].size;

        NF2FS_size_t cnt = 0;
        while (rest_size > 0) {
            // Calculate the number of sequential sectors the index has.
            NF2FS_size_t size = NF2FS_min(NF2FS->cfg->sector_size - off, rest_size);
            cnt++;
            rest_size -= size;
            off += size;
            if (off == NF2FS->cfg->sector_size)
                off = sizeof(NF2FS_bfile_sector_flash_t);
        }

        // Set all these sequential sectors to old.
        err = NF2FS_sequen_sector_old(NF2FS, index[i].sector, cnt);
        if (err)
            return err;
    }
    return err;
}

// erase a normal sector, should return the origin head to help building a new shead
// return true if we truly erase it.
bool NF2FS_sector_erase(NF2FS_t* NF2FS, NF2FS_size_t sector, NF2FS_head_t* head)
{
    int err= NF2FS_ERR_OK;

    // Read sector head of the sector.
    NF2FS_ASSERT(sector < NF2FS->cfg->sector_count);
    err= NF2FS_direct_read(NF2FS, sector, 0, sizeof(NF2FS_head_t), head);
    NF2FS_ASSERT(err == NF2FS_ERR_OK);

    // we do not need to erase if it has no data
    if (*head == NF2FS_NULL)
        return true;

    // Erase it if current sector has data.
    // When a id map sector has beed freed, it only records the etimes but not NF2FS_NULL
    if (NF2FS_shead_check(*head, NF2FS_STATE_FREE, NF2FS_SECTOR_NOTSURE)) {
        err = NF2FS->cfg->erase(NF2FS->cfg, sector);
        NF2FS_ASSERT(err == NF2FS_ERR_OK);
        
        // set the bit in erae map to 0
        err= NF2FS_emap_set(NF2FS, NF2FS->manager, sector, 1);
        NF2FS_ASSERT(err == NF2FS_ERR_OK);

        return true;
    }

    return false;
}

// erase sectors belonged to id/sector map without shead
int NF2FS_map_sector_erase(NF2FS_t* NF2FS, NF2FS_size_t begin, NF2FS_size_t num, NF2FS_size_t* etimes)
{
    int err = NF2FS_ERR_OK;

    NF2FS_head_t head;
    for (int i = 0; i < num; i++) {
        // Erase old one directly.
        err= NF2FS->cfg->erase(NF2FS->cfg, begin);
        if (err)
            return err;

        // Prog new sector head, valid flag 0 is used for NF2FS_shead_check
        // to ensure that the sector is free to use with etimes recording
        head = NF2FS_MKSHEAD(0, NF2FS_STATE_FREE, NF2FS_SECTOR_NOTSURE, 0x3f, etimes[i] + 1);
        err= NF2FS_direct_prog(NF2FS, NF2FS_DIRECT_PROG_SHEAD, begin, 0,
                              sizeof(NF2FS_head_t), &head);
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
int NF2FS_region_migration(NF2FS_t* NF2FS, NF2FS_size_t region_1, NF2FS_size_t region_2)
{
    // TODO in the future, currently wl is not finished
    return NF2FS_ERR_OK;
}

// GC for a dir
int NF2FS_dir_gc(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir)
{
    int err = NF2FS_ERR_OK;
    NF2FS_size_t old_tail= dir->tail_sector;

    // flush data to flash first.
    err= NF2FS_cache_flush(NF2FS, NF2FS->pcache);
    if (err)
        return err;

    // set son file's old index/data to delete
    NF2FS_file_ram_t* file= NF2FS->file_list;
    while (file != NULL) {
        if (file->father_id == dir->id) {
            err= NF2FS_data_delete(NF2FS, dir->id, file->file_cache.sector, file->file_cache.off,
                                  NF2FS_dhead_dsize(*(NF2FS_head_t *)file->file_cache.buffer));
            if (err)
                return err;
        }
        file= file->next_file;
    }

    // Starting gc.
    err = NF2FS_dtraverse_gc(NF2FS, dir);
    if (err)
        return err;

    // flush opened son file to flash
    file= NF2FS->file_list;
    while (file != NULL) {
        if (file->father_id == dir->id) {
            // prog new data/index to flash
            NF2FS_head_t old_head = *(NF2FS_head_t *)file->file_cache.buffer;
            NF2FS_head_t *head = (NF2FS_head_t *)file->file_cache.buffer;
            *head= NF2FS_MKDHEAD(0, 1, file->id, NF2FS_dhead_type(old_head), file->file_cache.size);
            err= NF2FS_dir_prog(NF2FS, dir, file->file_cache.buffer, file->file_cache.size);
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
    err = NF2FS_dir_old(NF2FS, old_tail);
    if (err)
        return err;

    // update dir message in its father dir.
    err = NF2FS_dir_update(NF2FS, dir);
    return err;
}

// GC for big file
int NF2FS_bfile_gc(NF2FS_t *NF2FS, NF2FS_file_ram_t *file)
{
    int err = NF2FS_ERR_OK;

    NF2FS_bfile_index_flash_t *bfile_index = (NF2FS_bfile_index_flash_t *)file->file_cache.buffer;
    NF2FS_size_t num = (file->file_cache.size - sizeof(NF2FS_size_t)) /
                        sizeof(NF2FS_bfile_index_ram_t);

    // No need to do gc
    NF2FS_ASSERT(num < NF2FS_FILE_INDEX_MAX);
    if (num < NF2FS_FILE_INDEX_NUM)
        return err;

    // find candidate indexes that can be gc
    NF2FS_size_t candidate_arr[NF2FS_FILE_INDEX_NUM] = {0};
    NF2FS_size_t arr_len = 0;
    for (int i = 0; i < num; i++) {
        if (bfile_index->index[i].size <= NF2FS->cfg->sector_size) {
            candidate_arr[arr_len] = i;
            arr_len++;
        }
    }
    NF2FS_ASSERT(arr_len <= NF2FS_FILE_INDEX_NUM);

    NF2FS_size_t gc_size = 0;
    NF2FS_size_t min, max;
    NF2FS_size_t distance = 0;
    for (int i = 0; i < arr_len; i++) {
        for (int j= i + 1; j < arr_len; j++) {
            // cal size that can be gc to merge these candidate indexes
            gc_size = 0;
            for (int k = candidate_arr[i]; k <= candidate_arr[j]; k++)
                gc_size += bfile_index->index[k].size;

            // loop to get the max distance, which means gc can reduce most indexes
            if (gc_size < NF2FS->manager->region_size * NF2FS->cfg->sector_size) {
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
    NF2FS_size_t len = NF2FS->cfg->sector_size - sizeof(NF2FS_bfile_sector_flash_t);
    gc_size = 0;
    for (int i = min; i <= max; i++)
        gc_size += bfile_index->index[i].size;
    NF2FS_size_t gc_num = NF2FS_alignup(gc_size, len) / len;

    // gc for part of big file.
    err = NF2FS_bfile_part_gc(NF2FS, file, min, max, gc_size, num, gc_num);
    if (err) {
        NF2FS_ERROR("NF2FS_bfile_part_gc WRONG!\n");
        return err;
    }
    return err;
}
