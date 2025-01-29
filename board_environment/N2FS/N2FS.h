#ifndef N2FS_H
#define N2FS_H

#include <stdbool.h>
#include <stdint.h>
#include "N2FS_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * --------------------------------------------------------------    Version info    -------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * The version of N2FS.
 *  1. Major (the top 16 bits), incremented on backwards incompatible changes
 *  2. Minor (the bottom 16 bits), incremented on feature additions
 */
#define N2FS_VERSION 0x00010000
#define N2FS_VERSION_MAJOR (0xffff & (N2FS_VERSION >> 16))
#define N2FS_VERSION_MINOR (0xffff & (N2FS_VERSION >> 0))

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    Redefine data type    ----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * The basic type used in N2FS.
 *
 *  1.  Size means read/prog/cache/sector/name/... size, i.e all size used in N2FS
 *  2.  Off means the address in a sector and other offset.
 *  3.  Head is the basic structure that describes information of data behind.
 *  4.  Hash is used in name resolution when the name is too long.
 *
 * S in ssize(soff) means signed, so the data could be negative.
 */
typedef uint32_t N2FS_size_t;
typedef int32_t N2FS_ssize_t;
typedef uint32_t N2FS_off_t;
typedef int32_t N2FS_soff_t;
typedef uint32_t N2FS_head_t;
typedef uint32_t N2FS_hash_t;

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------    Defination of some basic value    ----------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * The name of the filesystem
 */
#ifndef N2FS_FS_NAME
#define N2FS_FS_NAME "N2FS"
#endif

/**
 * It indicates that the sector/off/head.... is no use.
 */
#ifndef N2FS_NULL
#define N2FS_NULL 0xffffffff
#endif

/**
 * Maximum name size in bytes
 * The configuration of users' shouldn't large than it.
 */
#ifndef N2FS_NAME_MAX
#define N2FS_NAME_MAX 255
#endif

/**
 * Maximum size of a file in bytes
 *
 * The configuration of users' shouldn't large than it(32MB).
 */
#ifndef N2FS_FILE_MAX_SIZE
#define N2FS_FILE_MAX_SIZE 33554432
#endif

/**
 * Maximum size of id used in N2FS
 *
 * All files/dir stored in N2FS need a unique id to identify, and N2FS_ID_MAX shows
 * the max size of it. The following id used specially:
 *  1. ID 0: Used for superblock message.
 *  2. ID 1: Used for root dir.
 */
#ifndef N2FS_ID_MAX
#define N2FS_ID_MAX 8192
#endif

/**
 * Too many regions is unnecessay.
 * The number of region should be 2^n
 */
#ifndef N2FS_REGION_NUM_MAX
#define N2FS_REGION_NUM_MAX 1024
#endif

/**
 * The number of candidate regions we store in ram.
 * It's only used when wl starts.
 */
#ifndef N2FS_RAM_REGION_NUM
#define N2FS_RAM_REGION_NUM 4
#endif

/**
 * The beginning of the second-phase WL when it has
 * scanned NOR flash N2FS_WL_START times.
 */
#ifndef N2FS_WL_START
#define N2FS_WL_START 3000
#define N2FS_WL_MIGRATE_THRESHOLD (2 * N2FS_RAM_REGION_NUM * 50)
#endif

/**
 * The max number of file we could open at a time in ram.
 */
#ifndef N2FS_FILE_LIST_MAX
#define N2FS_FILE_LIST_MAX 5
#endif

/**
 * The max number of dir we could store at a time in ram.
 * TODO
 */
#ifndef N2FS_DIR_LIST_MAX
#define N2FS_DIR_LIST_MAX 10
#endif

/**
 * The sequential number of sectors allocated to wl array and wl added message.
 */
#ifndef N2FS_WL_SECTOR_NUM
#define N2FS_WL_SECTOR_NUM 1
#endif

/**
 * The length of space reserved for name in hash tree entry.
 */
#ifndef N2FS_ENTRY_NAME_LEN
#define N2FS_ENTRY_NAME_LEN 12
#endif

#ifndef N2FS_DHEAD_WRITTEN_SET
#define N2FS_DHEAD_WRITTEN_SET 0xbfffffff
#define N2FS_DHEAD_DELETE_SET 0xfffe0fff
#define N2FS_SHEAD_OLD_SET 0x87ffffff
#define N2FS_SHEAD_USING_SET 0x8fffffff
#endif

#ifndef N2FS_FILE_SIZE_THRESHOLD
#define N2FS_FILE_SIZE_THRESHOLD 64
#endif

// The file cache size should be larger, otherwise GC will be too frequent
#ifndef N2FS_FILE_CACHE_SIZE
#define N2FS_FILE_CACHE_SIZE 512
#endif

#ifndef N2FS_NORMAL_CACHE_SIZE
#define N2FS_NORMAL_CACHE_SIZE 256
#endif

// Used for big file GC
#ifndef N2FS_FILE_INDEX_NUM
#define N2FS_FILE_INDEX_NUM 20
#define N2FS_FILE_INDEX_MAX 42
#endif

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ---------------------------------------------------------------    Enum type    ---------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * Possible error types.
 *  1. 0 means no error.
 *  2. Negative data could tell us what's happening.
 */
enum N2FS_error
{
    N2FS_ERR_OK= 0, // No error
    N2FS_ERR_IO= -5, // Error during device operation
    N2FS_ERR_NOSPC= -6, // No space left on device
    N2FS_ERR_NOMEM= -7, // No more memory available
    N2FS_ERR_NODATA= -8, // No data in flash, should format

    N2FS_ERR_NOID= -20, // No more id to use.
    N2FS_ERR_NAMETOOLONG= -21, // File name too long
    N2FS_ERR_MUCHOPEN= -22, // The opened file/dir is too much.
    N2FS_ERR_NOFATHER= -23, // Don't have father dir.
    N2FS_ERR_NOENT= -24, // No directory entry
    N2FS_ERR_EXIST= -25, // Entry already exists
    N2FS_ERR_NOTDIR= -26, // Entry is not a dir
    N2FS_ERR_ISDIR= -27, // Entry is a dir
    N2FS_ERR_NOTEMPTY= -28, // Dir is not empty
    N2FS_ERR_BADF= -29, // Bad file number
    N2FS_ERR_FBIG= -30, // File too large
    N2FS_ERR_INVAL= -31, // Invalid parameter
    N2FS_ERR_NODIROPEN= -32, // Dir is not opened
    N2FS_ERR_NOFILEOPEN= -33, // File is not opened

    N2FS_ERR_CORRUPT= -50, // Corrupted has happened, and the read data is wrong.
    N2FS_ERR_WRONGCAL= -51, // Calculation has something wrong.
    N2FS_ERR_WRONGCFG= -52, // Cfg message is not the same as what stored in nor flash.
    N2FS_ERR_WRONGHEAD= -53, // Has a wrong head.
    N2FS_ERR_WRONGPROG= -54, // What we prog is wrong.
    N2FS_ERR_NOTINLIST= -55, // Not find in the list.

    N2FS_ERR_DIRHASH= -56, // It's not true error, but not we think it is for there is no
    N2FS_ERR_CANTDELETE= -57, // Now we can't delete on structure if it still has son.
    N2FS_ERR_TENTRY_NOFOUND= -58, // Not find the tree entry in the tree.
};

/**
 * The valid bit in flash.
 *
 * The origin bit in nor flash is 1, and we can turn it to 0 when program. So the valid
 * bit in flash is 0, which is contrary to the valid bit in ram(1).
 */
enum
{
    N2FS_FLASH_VALID= 0,
    N2FS_FLASH_NOTVALID= 1,
};

/**
 * The bit in sector map that tell us whether or not sector is valid.
 *
 * In free map, 1 indicates that the sector is free and it turns to 0 when we use.
 * In erase map, 1 indicates that the sector is used and it turn to 0 when it can be reused.
 */
enum
{
    N2FS_FREEMAP_VALID= 0,
    N2FS_ERASEMAP_VALID= 1,
};

// The specific use of id.
enum
{
    N2FS_ID_SUPER= 0,
    N2FS_ID_ROOT= 1,
};

// The open type of dir and file.
enum
{
    N2FS_USER_OPEN= 23,
    N2FS_MIGRATE= 54,
};

/**
 * Sector head for all sectors in nor flash.
 *
 *      [----            32             ----]
 *      [1|- 4 -|- 3 -|--  6  --|--  18   --]
 *      ^    ^     ^       ^          ^- Erase times
 *      |    |     |       '------------ Extend message, depends on type of sector.
 *      |    |     '-------------------- Type of sector.
 *      |    '-------------------------- State of Sector.
 *      '------------------------------- Valid bit, is N2FS_VLAID_FLASH(0).
 *
 *  1. Valid bit(1 bit): Tell us the head is valid.
 *
 *  2. State(4 bits):
 *
 *      1) free:   0xf
 *      2) wl:     0xb, is now swapping data while WL
 *      3) gc-ing: 0x7, tell us that current sector now is used for data migration diromg gc
 *      4) allocating:  0x3, the sector is allocated for dir/file but hasn't persisted by an entry yet.
 *      5) using:  0x1, the current sector is still using
 *      6) old:    0x0, the sector in no need, could be gc.
 *
 *    By turning 1 to 0, we could change the state of sector. There are two typical changes
 *
 *      1) free -> using -> old.
 *      2) free -> gc-ing -> ready -> using -> old.
 *
 *    Notice that free and ready is the same state.
 *  3. Type(3 bits):
 *
 *      1) super:   0x0, used to store the superblock message, always sector 0 and sector 1.
 *      2) dir:     0x1, used to store dir, small file(like 4B data) of dir is also in it
 *      3) big file:0x2, used to store big file, which is always larger than 1KB(or other size).
 *
 *    Though superblock means sector 0 and 1, we still need to use other sectors, like:
 *
 *      1) id map:      Don't have head, be pointed by pointer in sector 0/1
 *                      If has and map just occupy a sector, because of it, we can't store map in a sector.
 *      2) sector map:  The same as above.
 *      3) dir hash:    Used for name resolution, has head and is pointed by pointer in sector 0/1.
 *
 *  4. Extend message(6 bits):
 *
 *      1) super:   For sector 0 and 1, storing version number. When corrupt happens during gc of
 *                  superblock, it helps us to judge which secter(0 or 1) is now useful,
 *                  i.e choosing the smaller one and then continuing gc. We don't use 0x3f, when
 *                  one sector is 0x3e and the other is 0x00, we think 0x3e is smaller than 0x00.
 *                  For dir hash, store 0x3f to express it is used to store dir hash.
 *      2) dir:     Now no use.
 *      3) big file:Now no use.
 *
 *  5. Erase times(18 bits): Erase time of current sector.
*/

/**
 * The possible state of sector.
 * The head of N2FS_SECTOR_FREE could be 0xffffffff(When first access the sector).
*/
enum N2FS_sector_state
{
    N2FS_STATE_FREE= 0xf,
    N2FS_STATE_WL= 0xb,
    N2FS_STATE_GC= 0X7,
    N2FS_STATE_ALLOCATING= 0X3,
    N2FS_STATE_USING= 0X1,
    N2FS_STATE_OLD= 0X0,
};

/**
 * The possible type of sector
 * N2FS_SECTOR_MAP maybe no use because it does not have sector head
 */
enum N2FS_sector_type
{
    N2FS_SECTOR_SUPER= 0x0,
    N2FS_SECTOR_DIR= 0x1,
    N2FS_SECTOR_BFILE= 0x2,
    N2FS_SECTOR_WL= 0x4,
    N2FS_SECTOR_MAP= 0x5,
    N2FS_SECTOR_RESERVE= 0x6,
    N2FS_SECTOR_NOTSURE= 0x7,

    // Only a in-ram flag for reserve region.
    N2FS_SECTOR_META= 0xa,
};

/**
 * Data head for all datas stored in nor flash.
 *
 *      [----            32             ----]
 *      [1|1|--   13   --|- 5 -|--   12   --]
 *      ^ ^        ^        ^         ^- Length of data behind
 *      | |        |        '----------- Type of data
 *      | |        '-------------------- ID of data
 *      | '----------------------------  Writen flag
 *      '------------------------------  Valid bit, is N2FS_VLAID_FLASH(0).
 *
 *  1. Valid bit(1 bit):   Tell us the head is valid.
 *
 *  2. Writen flag(1 bit): Turned to 0 if we have already writen data head and data behind.
 *
 *  3. ID(14 bits):        A unique idenfication of file/dir/superblock
 *
 *  4. Type(5 bits):
 *
 *      1) N2FS_DATA_DIR_NAME:     The name of dir, in the front of the dir sector.
 *      2) N2FS_DATA_FILE_NAME:    The name of file.
 *      3) N2FS_DATA_DIR_ID：      The id of sub dir in father dir.
 *      4) N2FS_DATA_BIG_FILE:     The index of big file, real data is stored in another sector.
 *      5) N2FS_DATA_SMALL_FILE:   The data of small file, data is just behind the head.
 *      6) N2FS_DATA_DELETE:       The file/dir has been deleted, all data belongs to it should turn to this.
 *
 *      7) N2FS_DATA_WL:           The message about WL, construed when we loop the sector map more than a number.
 *      8) N2FS_DATA_DIR_HASH:     Helping us achiving name resolution quickly to find out the id of dir.
 *                                   Only contains dir, no file, could be considered as dir index structure.
 *      9) N2FS_DATA_SECTOR_MAP:   Tell us which sector could be used: free, ready and old. Old needs erase.
 *      10)N2FS_DATA_ERASE_MAP:    Tell us which sector could be erased. When a loop of sector map finished,
 *                                   can be the next sector map(just turn 0x33 to 0x31, i.e a bit 1 to bit 0).
 *      11)N2FS_DATA_ID_MAP:       Tell us which id could be used.
 *      12)N2FS_DATA_REMOVE_MAP:   Tell us which file/dir is removed, so we could reuse the id. When a loop of
 *                                   id map finished, can be the next id map(similar to erase map).
 *
 *      13)N2FS_DATA_SUPER_MESSAGE:Basic message stored in sector 0/1, and it's in the front of it.
 *      14)N2FS_DATA_MOUNT_MESSAGE:Stored message like where in sector(id) map we are scaning now.
 *      15)N2FS_DATA_MAGIC:        Tell us whether or not corrupt happens. When mount, the data behind
 *                                   turned to 0; When umount, write a new one.
 *
 *  5. Length(10 bits):    Length of data behind, 0x3ff is not used and sometimes means something.
 */

/**
 * The possible type of data.
 */
enum N2FS_data_type
{
    N2FS_DATA_FREE= 0x1f,
    N2FS_DATA_SUPER_MESSAGE= 0X1e,
    N2FS_DATA_COMMIT= 0X1d,
    N2FS_DATA_MAGIC= 0X1c,

    N2FS_DATA_SECTOR_MAP= 0x19,
    N2FS_DATA_ID_MAP= 0x18,
    N2FS_DATA_REGION_MAP= 0x17,
    N2FS_DATA_WL_ADDR= 0X16,
    N2FS_DATA_TREE_ADDR= 0x15,

    // New DIR/FILE NAME is used to free id when crash occurs at a new creation.
    N2FS_DATA_NDIR_NAME= 0x14,
    N2FS_DATA_NFILE_NAME= 0x13,
    N2FS_DATA_DIR_NAME= 0x0e,
    N2FS_DATA_FILE_NAME= 0x0c,
    N2FS_DATA_BFILE_INDEX= 0x0b,
    N2FS_DATA_SFILE_DATA= 0x0a,
    N2FS_DATA_DIR_OSPACE= 0x09,

    // N2FS_DATA_DELETE can also be the end of readdir
    N2FS_DATA_DELETE= 0x00, 
    N2FS_DATA_REG= 0x02,
    N2FS_DATA_DIR= 0x01,
};

/**
 * File seek flags, used in N2FS_seek function.
 */
enum N2FS_whence_flags
{
    N2FS_SEEK_SET= 0, // Seek relative to an absolute position
    N2FS_SEEK_CUR= 1, // Seek relative to the current file position
    N2FS_SEEK_END= 2, // Seek relative to the end of the file
};

/**
 * The comparasion result, i.e equal, less, greater.
 */
enum
{
    N2FS_CMP_EQ= 0,
    N2FS_CMP_LT= 1,
    N2FS_CMP_GT= 2,
};

/**
 * In hash tree, if name is too long, we use hash instead.
 */
enum
{
    N2FS_TREE_HASH= 0,
    N2FS_TREE_NAME= 1,
};

/**
 * The first three are used in N2FS_direct_prog to judge the write type
 * The next two are used in N2FS_dprog_cache_sync
 */
enum
{
    N2FS_DIRECT_PROG_SHEAD= 0,
    N2FS_DIRECT_PROG_DHEAD= 1,
    N2FS_DIRECT_PROG_DATA= 2,
    N2FS_DPROG_CACHE_HEAD_CHANGE= 3,
    N2FS_DPROG_CACHE_DATA_PROG= 4,
};

enum
{
    N2FS_REGION_MAP_NOCHANGE= 0,
    N2FS_REGION_MAP_IN_PLACE_CHANGE= 1,
    N2FS_REGION_MAP_NEW_MAP= 2,
};

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Configure structure    ----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * Basic configure message provided by user during mount.
 */
typedef struct N2FS_config
{
    // Read data in (sector, off) to buffer.
    // Negative error codes are propagated to user, also for the following functions.
    int (*read)(const struct N2FS_config* c, N2FS_size_t sector, N2FS_off_t off, void* buffer, N2FS_size_t size);

    // write(program) data in (sector, off) to buffer.
    // May return N2FS_ERR_CORRUPT if the sector should be considered bad.
    int (*prog)(const struct N2FS_config* c, N2FS_size_t sector, N2FS_off_t off, void* buffer, N2FS_size_t size);

    // Erase a sector.
    // A sector must be erased before being programmed.
    // May return N2FS_ERR_CORRUPT if the sector should be considered bad.
    int (*erase)(const struct N2FS_config* c, N2FS_size_t sector);

    // Sync the state of the underlying block device.
    int (*sync)(const struct N2FS_config* c);

#ifdef N2FS_THREADSAFE
    // Lock the underlying sector device.
    int (*lock)(const struct N2FS_config* c);

    // Unlock the underlying sector device.
    int (*unlock)(const struct N2FS_config* c);
#endif

    // Minimum size of a sector read in bytes.
    // All read operations will be a multiple of this value.
    N2FS_size_t read_size;

    // Minimum size of a sector program in bytes.
    // All program operations will be a multiple of this value.
    N2FS_size_t prog_size;

    // Size of an erasable sector in bytes.
    // This does not impact ram consumption and may be larger than the physical erase size.
    // However, non-inlined files take up at minimum one sector. Must be a multiple of the
    // read and program sizes.
    N2FS_size_t sector_size;

    // Number of erasable sectors on the device.
    N2FS_size_t sector_count;

    // Size of sector caches in bytes. Each cache buffers a portion of a sector in
    // RAM.Larger caches can improve performance by storing moredata and reducing
    // the number of disk accesses. Must be a multiple of theread and program
    // sizes, and a factor of the sector size.
    N2FS_size_t cache_size;

    // We split Sectors into several regions. Every time we choose a region with
    // least P/E to guarantee WL, and then scan the region to build a bit map in DRAM
    N2FS_size_t region_cnt;

    // Optional upper limit on length of file names in bytes. No downside for
    // larger names except the size of the info struct which is controlled by
    // the N2FS_NAME_MAX define. Defaults to N2FS_NAME_MAX when zero. Stored in
    // superblock and must be respected by other N2FS drivers.
    N2FS_size_t name_max;

    // Optional upper limit on files in bytes. No downside for larger files
    // but must be <= N2FS_FILE_MAX. Defaults to N2FS_FILE_MAX when zero. Stored
    // in superblock and must be respected by other N2FS drivers.
    N2FS_size_t file_max;
} N2FS_config_t;

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Sector head structure    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * The common head structure for all sectors belong to dir.
 *  1. head describes the basic message of sector.
 *  2. pre_sector links sectors if they belong to the same dir.
 */
typedef struct N2FS_dir_sector_flash
{
    N2FS_head_t head;
    N2FS_size_t pre_sector;
    N2FS_size_t id;
} N2FS_dir_sector_flash_t;

/**
 * The common head structure for all sectors belong to big file.
 * We only need a head, because these sectors will be organized by a file index structure.
 */
typedef struct N2FS_bfile_sector_flash
{
    N2FS_head_t head;
    N2FS_size_t id;
    N2FS_size_t father_id;
} N2FS_bfile_sector_flash_t;

/**
 * The common head structure for sectors belong to all types of super message.
 * Similar to big file sector.
 */
typedef struct N2FS_super_sector_flash
{
    N2FS_head_t head;
} N2FS_super_sector_flash_t;

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Superblock structure    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * The in ram structure that describes the baisc information of superblock that used.
 */
typedef struct N2FS_superblock_ram
{
    N2FS_size_t sector;
    N2FS_off_t free_off;
} N2FS_superblock_ram_t;

/**
 * The basic message about N2FS.
 *
 *  1. fs_name stores "N2FS", tells us if nor flash could be used by N2FS.
 *
 *  2. If the version in nor flash is different from N2FS we use now, there
 *     might be some problem.
 *
 *  3. We divide sectors into some regions(like 256) to easy to manage, and
 *     save ram space.
 */
typedef struct N2FS_supermessage_flash
{
    N2FS_head_t head;
    uint32_t version;
    N2FS_size_t sector_size;
    N2FS_size_t sector_count;
    N2FS_size_t name_max;
    N2FS_size_t file_max; // the max file size
    N2FS_size_t region_cnt;
    uint8_t fs_name[5];
} N2FS_supermessage_flash_t;

/**
 * The map of all regions.
 * Because the max size of the map is just 128 / 8 = 16B,
 * we could just store it in super sector 0/1.
 */
typedef struct N2FS_region_map_flash
{
    N2FS_head_t head;
    uint8_t map[];
} N2FS_region_map_flash_t;

/**
 * The position of bit map in nor flash.
 * We should also record erase times of the sector.
 */
typedef struct N2FS_mapaddr_flash
{
    N2FS_head_t head;
    N2FS_size_t begin;
    N2FS_off_t off;
    N2FS_size_t erase_times[];
} N2FS_mapaddr_flash_t;

/**
 * The position of wl message in nor flash.
 */
typedef struct N2FS_wladdr_flash
{
    N2FS_head_t head;
    N2FS_size_t begin;
    N2FS_size_t off;
    N2FS_size_t erase_times;
} N2FS_wladdr_flash_t;

/**
 * Every time we umount or commit(maybe have), we should write this.
 *
 * wl_index and wl_free_off only used when wl works.
 *  1) Index is the index for wl array ordered by erase times.
 *  2) Wl_free_off is the free space for added wl message.
 *
 * Whether or not record free off:
 *  1) For all maps, we don't need to append write, so free off is no need.
 *  2) For hash tree, we should traverse all hash tree entry to construct
 *     in-ram hash structure, so don't need to record the free off.
 *  3) But we don't want to traverse wl array and added message behind, so we
 *     should record free off.
 */
typedef struct N2FS_commit_flash
{
    N2FS_head_t head;
    N2FS_size_t next_id;
    N2FS_size_t scan_times;
    N2FS_size_t next_dir_sector;
    N2FS_size_t next_bfile_sector;
    N2FS_size_t reserve_region;
} N2FS_commit_flash_t;

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ------------------------------------------------------------    Cache structure    ------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * In ram structure.
 * Store nor flash data of (sector, off) in buffer, the size is size.
 * The mode tells us data in buffer is writen forward or backward.
 */
typedef struct N2FS_cache_ram
{
    N2FS_size_t sector;
    N2FS_off_t off;
    N2FS_size_t size;
    bool change_flag;
    uint8_t* buffer;
} N2FS_cache_ram_t;

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Map and wl structure    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * The management structure of regions.
 *
 *  1) We reserve a region for gc and data migration.
 *  2) dir_region and bfile_region tells us which region belongs
 *     to dir or big file.
 *  3) The three regions are all map structure, and for small size
 *     nor flash like 1MB, we may only use 8 bits of a uint32_t structure.
 */
typedef struct N2FS_region_map_ram
{
    N2FS_size_t begin;
    N2FS_off_t off;
    uint32_t change_flag; // in-place update or other places

    N2FS_size_t reserve;

    N2FS_off_t dir_index;
    uint32_t* dir_region;

    N2FS_off_t bfile_index;
    uint32_t* bfile_region;
} N2FS_region_map_ram_t;

/**
 * The basic bit map structure for free sector/erase sector/free id/remove id map.
 *
 *  1. (begin, off) is the map address in nor flash.
 *  2. For sector map
 *      1) region is which region's bit map that stroed in buffer.
 *      2) Index is the position now we scan in buffer.
 *  3. For id map
 *      1) region tells us whether or not we should update ram map to flash.
 *      2) We configure buffer size that is bits_in_buffer.
 *  4. For region map
 *      1) region is the number of bits in buffer.(may no use)
 *         Because of the max number of region is 128, it's not so big.
 */
typedef struct N2FS_map_ram
{
    N2FS_size_t region;
    N2FS_size_t index_or_changed; // In erase map tells us there are changes.
    N2FS_size_t free_num;
    uint32_t buffer[];
} N2FS_map_ram_t;

// TODO
// change the wl structure
// for sort pool, (region, etimes) is still needed, but others should be region only
// moreover the manage module should like map

/**
 * Record the total erase times of sectors in the region.
 */
typedef struct N2FS_wl_message
{
    N2FS_size_t region;
    N2FS_size_t etimes;
} N2FS_wl_message_t;

/**
 * The in ram wl structure.
 *
 * Index tells us now we choose N2FS_wl_entry_flash_t[index % 5] of sorted array.
 * When index larger that threshold, turn it back to 0.
 *
 * num is the sequential number of sectors that wl has.
 * free_off is the offset for free space in wl sector, can larger than 4KB.
 *
 * We not only use regions to record canditate region, but also record erase times
 * it increased.
 *
 * When changed_region_times >= 2 * N2FS_RAM_REGION_NUM * scan_throld, data migration
 */
typedef struct N2FS_wl_ram
{
    N2FS_size_t begin;
    N2FS_off_t off;
    N2FS_size_t etimes;

    N2FS_size_t changed_region_times;
    N2FS_size_t dir_region_index;
    N2FS_size_t bfile_region_index;
    N2FS_size_t dir_regions[N2FS_RAM_REGION_NUM];
    N2FS_size_t bfile_regions[N2FS_RAM_REGION_NUM];
} N2FS_wl_ram_t;

/**
 * The management structure of nor flash.
 */
typedef struct N2FS_flash_manage_ram
{
    N2FS_size_t region_num;
    N2FS_size_t region_size;
    N2FS_size_t scan_times;

    N2FS_size_t smap_begin;
    N2FS_off_t smap_off; // The offset of in-NOR sector map, not erase map
    N2FS_size_t* etimes;

    N2FS_region_map_ram_t* region_map;

    N2FS_map_ram_t* meta_map;
    N2FS_map_ram_t* dir_map;
    N2FS_map_ram_t* bfile_map;
    N2FS_map_ram_t* reserve_map;
    N2FS_map_ram_t* erase_map;
    N2FS_wl_ram_t* wl;
} N2FS_flash_manage_ram_t;

/**
 * The bit map of id.
 */
typedef struct N2FS_idmap_ram
{
    N2FS_size_t begin;
    N2FS_size_t off;
    N2FS_size_t etimes;

    N2FS_size_t ids_in_buffer;
    N2FS_map_ram_t* free_map;
} N2FS_idmap_ram_t;

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    RAM tree structure    ----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * The structure of tree entry, size is 32 B
 */
typedef struct N2FS_tree_entry_ram
{
    N2FS_size_t id; // N2FS_NULL if invalid
    N2FS_size_t father_id;

    N2FS_size_t name_sector; // sector that store dir name
    N2FS_size_t name_off;
    N2FS_size_t tail_sector; // sector that belongs to the dir
    union data
    {
        N2FS_hash_t hash;
        uint8_t name[N2FS_ENTRY_NAME_LEN];
    } data;
} N2FS_tree_entry_ram_t;

typedef struct N2FS_tree_ram
{
    N2FS_size_t entry_num;
    N2FS_tree_entry_ram_t* tree_array;
} N2FS_tree_ram_t;

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ------------------------------------------------------------    File structure    -------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * When we write it to father dir, a file is created.
 */
typedef struct N2FS_file_name_flash
{
    N2FS_head_t head;
    uint8_t name[];
} N2FS_file_name_flash_t;

/**
 * The basic index structure for big file.
 * It's the same both in ram and in nor flash, and for small file, it's only in ram.
 */
typedef struct N2FS_bfile_index_ram
{
    N2FS_size_t sector;
    N2FS_off_t off;
    N2FS_size_t size; // Not include sector head message.
} N2FS_bfile_index_ram_t;

/**
 * The basic index structure for big file.
 * It's stored in dir. 
 */
typedef struct N2FS_bfile_index_flash
{
    N2FS_head_t head;
    N2FS_bfile_index_ram_t index[];
} N2FS_bfile_index_flash_t;

/**
 * The basic data structure structure of small file.
 * It's stored in dir.
 */
typedef struct N2FS_sfile_data_flash
{
    N2FS_head_t head;
    uint8_t data[];
} N2FS_sfile_data_flash_t;

/**
 * The in ram structure of file.
 *  2. file_pos is the logical position of file where we start to read/write(program).
 *     Could be changed by lseek function.
 *
 *  3. File cache has different usage for big / small file.
 *     For big file, it stores index in buffer.
 *     For small file, it stores all datas in nor flash.
 *     in the cache, (Sector, off) belongs to old data, size belongs to new message in buffer.
 *
 *  5. All file we stored in ram are linked by the next_file, and are sorted by
 *     the last time we use. If there is no more space, could delete file at the
 *     tail of the link list.
 */
typedef struct N2FS_file_ram
{
    N2FS_size_t id;
    N2FS_size_t father_id;
    N2FS_ssize_t file_size;
    N2FS_off_t file_pos;

    N2FS_size_t sector; // the path of file_name
    N2FS_off_t off;
    N2FS_size_t namelen;

    N2FS_cache_ram_t file_cache;
    struct N2FS_file_ram* next_file;
} N2FS_file_ram_t;

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------------    Dir structure    -------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// File info structure, used for reading dir entry
typedef struct N2FS_info
{
    uint8_t type;
    char name[N2FS_NAME_MAX + 1];
} N2FS_info_ram_t;

/**
 * The in ram structure of directory.
 *
 *  1. old_space tell us how many space can we gc if gc happends for the dir.
 *     not the accurate value, but the least.
 *
 *  2. The name of file and data are seperated. To quickly find file in data,
 *     we store all names in the front of the sector belongs to the dir, and
 *     all data in the end of it.
 *     The forward and backward pointer tell us where we should tail to write.
 *
 *  3. If a dir is very big, then we should use many sectors. All these sectors
 *     use a pointer at the end of each sector linked together.
 *     tail is the first sector we use for the dir.
 *     current_sector is the sector now we use to program.
 *     And rest_space tells us how many space there are in current sector.
 *
 *  4. All dir we stored in ram are linked by the next_dir, and are sorted by
 *     the last time we use. If there is no more space, could delete dir at the
 *     tail of the link list.
 *
 *  5. Backward list stores all backward start message of former sectors belong
 *     to dir.
 */
typedef struct N2FS_dir_ram
{
    N2FS_size_t id;
    N2FS_size_t father_id;

    N2FS_size_t old_sector; // position that records old space
    N2FS_size_t old_off;
    N2FS_size_t old_space;

    N2FS_size_t pos_sector; // used for dir read
    N2FS_size_t pos_off;
    N2FS_size_t pos_presector;

    N2FS_size_t name_sector;
    N2FS_size_t name_off;
    N2FS_size_t namelen;

    N2FS_size_t tail_sector;
    N2FS_off_t tail_off;

    struct N2FS_dir_ram* next_dir;
} N2FS_dir_ram_t;

/**
 * The name of dir, stored at the beginning of sectors that belong to the dir.
 */
typedef struct N2FS_dir_name_flash
{
    N2FS_head_t head;
    N2FS_size_t tail;
    uint8_t name[];
} N2FS_dir_name_flash_t;

// The old space that can be recycled for current dir.
typedef struct N2FS_dir_ospace_flash
{
    N2FS_head_t head;
    N2FS_size_t old_space;
} N2FS_dir_ospace_flash_t;

/**
 * -------------------------------------------------------------------------------------------------------
 * -------------------------------    Other ram fundamental structure    ---------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

/**
 * N2FS_t includes all source we use and all message we have.
 * It's the main ram structure of the fs.
 */
typedef struct N2FS
{
    N2FS_cache_ram_t* rcache;
    N2FS_cache_ram_t* pcache;

    N2FS_superblock_ram_t* superblock;
    N2FS_flash_manage_ram_t* manager;
    N2FS_tree_ram_t* ram_tree;
    N2FS_idmap_ram_t* id_map;

    N2FS_file_ram_t* file_list;
    N2FS_dir_ram_t* dir_list;

    const struct N2FS_config* cfg;
} N2FS_t;

/**
 * -------------------------------------------------------------------------------------------------------
 * --------------------------------------    FS level operations    --------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// format before first mounting
int N2FS_format(N2FS_t* N2FS, const struct N2FS_config* cfg, bool init_flag);

// mount N2FS
int N2FS_mount(N2FS_t* N2FS, const struct N2FS_config* cfg);

// unmount N2FS
int N2FS_unmount(N2FS_t* N2FS);

/**
 * -------------------------------------------------------------------------------------------------------
 * -------------------------------------    File level operations    -------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// open a file
int N2FS_file_open(N2FS_t* N2FS, N2FS_file_ram_t** file, char* path, int flags);

// close a file
int N2FS_file_close(N2FS_t* N2FS, N2FS_file_ram_t* file);

// read data of a file
int N2FS_file_read(N2FS_t* N2FS, N2FS_file_ram_t* file, void* buffer, N2FS_size_t size);

// write data to a file
int N2FS_file_write(N2FS_t* N2FS, N2FS_file_ram_t* file, void* buffer, N2FS_size_t size);

// change the file position
int N2FS_file_seek(N2FS_t* N2FS, N2FS_file_ram_t* file, N2FS_soff_t off, int whence);

// delete a file
int N2FS_file_delete(N2FS_t* N2FS, N2FS_file_ram_t* file);

// flush file data to flash
int N2FS_file_sync(N2FS_t* N2FS, N2FS_file_ram_t* file);

/**
 * -------------------------------------------------------------------------------------------------------
 * -------------------------------------    Dir level operations    --------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

// open a dir
int N2FS_dir_open(N2FS_t* N2FS, N2FS_dir_ram_t** dir, char* path);

// close a dir
int N2FS_dir_close(N2FS_t* N2FS, N2FS_dir_ram_t* dir);

// delete a dir
int N2FS_dir_delete(N2FS_t* N2FS, N2FS_dir_ram_t* dir);

// read an dir entry from dir.
int N2FS_dir_read(N2FS_t* N2FS, N2FS_dir_ram_t* dir, N2FS_info_ram_t* info);

#ifdef __cplusplus
}
#endif

#endif
