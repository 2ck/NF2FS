/**
 * The basic head operations of N2FS
 */

#ifndef N2FS_HEAD_H
#define N2FS_HEAD_H

#include "N2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    Sector head operations    -------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * Make sector head with the following message.
 */
#define N2FS_MKSHEAD(valid, state, type, extend, erase_times) \
    (((N2FS_head_t)(valid) << 31) | (N2FS_head_t)(state) << 27 | ((N2FS_head_t)(type) << 24) | ((N2FS_head_t)(extend) << 18) | (N2FS_head_t)(erase_times))

N2FS_size_t N2FS_shead_extend(N2FS_head_t shead);

N2FS_size_t N2FS_shead_etimes(N2FS_head_t shead);

N2FS_size_t N2FS_shead_type(N2FS_head_t shead);

int N2FS_shead_check(N2FS_head_t shead, N2FS_size_t state, int type);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    Data head operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * Make data head with the following message.
 */
#define N2FS_MKDHEAD(valid, writen, id, type, len) \
    (((N2FS_head_t)(valid) << 31) | ((N2FS_head_t)(writen) << 30) | ((N2FS_head_t)(id) << 17) | ((N2FS_head_t)(type) << 12) | (N2FS_head_t)(len))

N2FS_size_t N2FS_dhead_dsize(N2FS_head_t dhead);

int N2FS_dhead_check(N2FS_head_t dhead, N2FS_size_t id, int type);

N2FS_size_t N2FS_dhead_type(N2FS_head_t dhead);

N2FS_size_t N2FS_dhead_id(N2FS_head_t dhead);

#ifdef __cplusplus
}
#endif

#endif
