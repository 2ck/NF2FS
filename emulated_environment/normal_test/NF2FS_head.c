/**
 * The basic head operations of NF2FS
 */

#include "NF2FS_head.h"
#include "NF2FS.h"

/**
 * -------------------------------------------------------------------------------------------------------
 * -------------------------------------    Sector head operations    ------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

/**
 * Whether or not the sector head is valid.
 */
bool NF2FS_shead_novalid(NF2FS_head_t shead)
{
    return shead & 0x80000000;
}

/**
 * The state of sector head.
 */
NF2FS_size_t NF2FS_shead_state(NF2FS_head_t shead)
{
    return (shead & 0x78000000) >> 27;
}

/**
 * The use type of sector head.
 */
NF2FS_size_t NF2FS_shead_type(NF2FS_head_t shead)
{
    return (shead & 0x07000000) >> 24;
}

/**
 * The extend message of sector head.
 * For superblock, it's version number.
 */
NF2FS_size_t NF2FS_shead_extend(NF2FS_head_t shead)
{
    return (shead & 0x00fc0000) >> 18;
}

/**
 * The erase times of sector head.
 */
NF2FS_size_t NF2FS_shead_etimes(NF2FS_head_t shead)
{
    return shead & 0x0003ffff;
}

/**
 * Check whether the sector header is consistent with given parameters
 * NF2FS_NULL for shead means the sector hasn't be used
 * NF2FS_NULL for state, and type means we do not check them
 */
int NF2FS_shead_check(NF2FS_head_t shead, NF2FS_size_t state, int type)
{
    int err = NF2FS_ERR_OK;

    if (shead == NF2FS_NULL)
    {
        return err;
    }

    if (NF2FS_shead_novalid(shead))
    {
        err = NF2FS_ERR_WRONGHEAD;
        return err;
    }

    if (state != NF2FS_NULL && NF2FS_shead_state(shead) != state)
    {
        err = NF2FS_ERR_WRONGHEAD;
        return err;
    }

    if (type != NF2FS_NULL && NF2FS_shead_type(shead) != type)
    {
        err = NF2FS_ERR_WRONGHEAD;
        return err;
    }

    if (shead == 0)
        return NF2FS_ERR_WRONGHEAD;

    return err;
}

/**
 * -------------------------------------------------------------------------------------------------------
 * --------------------------------------    Data head operations    -------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

/**
 * Whether or not the data head is valid.
 */
bool NF2FS_dhead_novalid(NF2FS_head_t dhead)
{
    return dhead & 0x80000000;
}

/**
 * Whether or not the data head and data behind is finished.
 */
bool NF2FS_dhead_nowritten(NF2FS_head_t dhead)
{
    return dhead & 0x40000000;
}

/**
 * Get the id that the data head belongs to.
 */
NF2FS_size_t NF2FS_dhead_id(NF2FS_head_t dhead)
{
    return (dhead & 0x3ffe0000) >> 17;
}

/**
 * Get the type of data head and know what it's used to do.
 */
NF2FS_size_t NF2FS_dhead_type(NF2FS_head_t dhead)
{
    return (dhead & 0x0001f000) >> 12;
}

/**
 * Get the total size of dhead + following data.
 * Notice that dhead + NF2FS_dhead_isdelete(dhead) = 0x3ff + 1 = 0 when deleted.
 */
NF2FS_size_t NF2FS_dhead_dsize(NF2FS_head_t dhead)
{
    return dhead & 0x00000fff;
}

/**
 * Check whether the data header is consistent with given parameters
 * NF2FS_NULL for dhead means the sector hasn't be used
 * NF2FS_NULL for id, and type means we do not check them
 */
int NF2FS_dhead_check(NF2FS_head_t dhead, NF2FS_size_t id, int type)
{
    int err = NF2FS_ERR_OK;

    if (dhead == NF2FS_NULL)
        return err;

    if (NF2FS_dhead_novalid(dhead) || NF2FS_dhead_nowritten(dhead)) {
        err = NF2FS_ERR_WRONGHEAD;
        return err;
    }

    if (id != NF2FS_NULL && NF2FS_dhead_id(dhead) != id) {
        err = NF2FS_ERR_WRONGHEAD;
        return err;
    }

    if (type != NF2FS_NULL && NF2FS_dhead_type(dhead) != type) {
        err = NF2FS_ERR_WRONGHEAD;
        return err;
    }

    if (dhead == 0)
        return NF2FS_ERR_WRONGHEAD;

    return err;
}
