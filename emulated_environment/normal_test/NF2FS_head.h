/**
 * The basic head operations of NF2FS
 */

#ifndef NF2FS_HEAD_H
#define NF2FS_HEAD_H

#include "NF2FS.h"

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
#define NF2FS_MKSHEAD(valid, state, type, extend, erase_times) \
    (((NF2FS_head_t)(valid) << 31) | (NF2FS_head_t)(state) << 27 | ((NF2FS_head_t)(type) << 24) | ((NF2FS_head_t)(extend) << 18) | (NF2FS_head_t)(erase_times))

NF2FS_size_t NF2FS_shead_extend(NF2FS_head_t shead);

NF2FS_size_t NF2FS_shead_etimes(NF2FS_head_t shead);

NF2FS_size_t NF2FS_shead_type(NF2FS_head_t shead);

int NF2FS_shead_check(NF2FS_head_t shead, NF2FS_size_t state, int type);

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    Data head operations    --------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

/**
 * Make data head with the following message.
 */
#define NF2FS_MKDHEAD(valid, writen, id, type, len) \
    (((NF2FS_head_t)(valid) << 31) | ((NF2FS_head_t)(writen) << 30) | ((NF2FS_head_t)(id) << 17) | ((NF2FS_head_t)(type) << 12) | (NF2FS_head_t)(len))

NF2FS_size_t NF2FS_dhead_dsize(NF2FS_head_t dhead);

int NF2FS_dhead_check(NF2FS_head_t dhead, NF2FS_size_t id, int type);

NF2FS_size_t NF2FS_dhead_type(NF2FS_head_t dhead);

NF2FS_size_t NF2FS_dhead_id(NF2FS_head_t dhead);

#ifdef __cplusplus
}
#endif

#endif
