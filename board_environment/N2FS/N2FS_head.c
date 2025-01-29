/**
 * The basic head operations of N2FS
 */

#include "N2FS_head.h"
#include "N2FS.h"

/**
 * -------------------------------------------------------------------------------------------------------
 * -------------------------------------    Sector head operations    ------------------------------------
 * -------------------------------------------------------------------------------------------------------
 */

/**
 * Whether or not the sector head is valid.
 */
bool N2FS_shead_novalid(N2FS_head_t shead)
{
    return shead & 0x80000000;
}

/**
 * The state of sector head.
 */
N2FS_size_t N2FS_shead_state(N2FS_head_t shead)
{
    return (shead & 0x78000000) >> 27;
}

/**
 * The use type of sector head.
 */
N2FS_size_t N2FS_shead_type(N2FS_head_t shead)
{
    return (shead & 0x07000000) >> 24;
}

/**
 * The extend message of sector head.
 * For superblock, it's version number.
 */
N2FS_size_t N2FS_shead_extend(N2FS_head_t shead)
{
    return (shead & 0x00fc0000) >> 18;
}

/**
 * The erase times of sector head.
 */
N2FS_size_t N2FS_shead_etimes(N2FS_head_t shead)
{
    return shead & 0x0003ffff;
}

/**
 * Check whether the sector header is consistent with given parameters
 * N2FS_NULL for shead means the sector hasn't be used
 * N2FS_NULL for state, and type means we do not check them
 */
int N2FS_shead_check(N2FS_head_t shead, N2FS_size_t state, int type)
{
    int err = N2FS_ERR_OK;

    if (shead == N2FS_NULL)
    {
        return err;
    }

    if (N2FS_shead_novalid(shead))
    {
        err = N2FS_ERR_WRONGHEAD;
        return err;
    }

    if (state != N2FS_NULL && N2FS_shead_state(shead) != state)
    {
        err = N2FS_ERR_WRONGHEAD;
        return err;
    }

    if (type != N2FS_NULL && N2FS_shead_type(shead) != type)
    {
        err = N2FS_ERR_WRONGHEAD;
        return err;
    }

    if (shead == 0)
        return N2FS_ERR_WRONGHEAD;

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
bool N2FS_dhead_novalid(N2FS_head_t dhead)
{
    return dhead & 0x80000000;
}

/**
 * Whether or not the data head and data behind is finished.
 */
bool N2FS_dhead_nowritten(N2FS_head_t dhead)
{
    return dhead & 0x40000000;
}

/**
 * Get the id that the data head belongs to.
 */
N2FS_size_t N2FS_dhead_id(N2FS_head_t dhead)
{
    return (dhead & 0x3ffe0000) >> 17;
}

/**
 * Get the type of data head and know what it's used to do.
 */
N2FS_size_t N2FS_dhead_type(N2FS_head_t dhead)
{
    return (dhead & 0x0001f000) >> 12;
}

/**
 * Get the total size of dhead + following data.
 * Notice that dhead + N2FS_dhead_isdelete(dhead) = 0x3ff + 1 = 0 when deleted.
 */
N2FS_size_t N2FS_dhead_dsize(N2FS_head_t dhead)
{
    return dhead & 0x00000fff;
}

/**
 * Check whether the data header is consistent with given parameters
 * N2FS_NULL for dhead means the sector hasn't be used
 * N2FS_NULL for id, and type means we do not check them
 */
int N2FS_dhead_check(N2FS_head_t dhead, N2FS_size_t id, int type)
{
    int err = N2FS_ERR_OK;

    if (dhead == N2FS_NULL)
        return err;

    if (N2FS_dhead_novalid(dhead) || N2FS_dhead_nowritten(dhead)) {
        err = N2FS_ERR_WRONGHEAD;
        return err;
    }

    if (id != N2FS_NULL && N2FS_dhead_id(dhead) != id) {
        err = N2FS_ERR_WRONGHEAD;
        return err;
    }

    if (type != N2FS_NULL && N2FS_dhead_type(dhead) != type) {
        err = N2FS_ERR_WRONGHEAD;
        return err;
    }

    if (dhead == 0)
        return N2FS_ERR_WRONGHEAD;

    return err;
}
