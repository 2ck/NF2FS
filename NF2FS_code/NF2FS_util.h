#ifndef NF2FS_UTIL_H
#define NF2FS_UTIL_H

// System includes
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
// #include "FreeRTOS.h"

#ifndef NF2FS_NO_MALLOC
#include <stdlib.h>
#endif
#ifndef NF2FS_NO_ASSERT
#include <assert.h>
#endif
#if !defined(NF2FS_NO_DEBUG) || !defined(NF2FS_NO_WARN) || !defined(NF2FS_NO_ERROR) || defined(NF2FS_YES_TRACE)
#include <stdio.h>
#endif

#define inline __inline

#ifdef __cplusplus
extern "C" {
#endif

// print error message
#ifndef NF2FS_ERROR
#define NF2FS_ERROR(fmt, ...) printf("%s:%d:error: " fmt "\n", __FILE__, __LINE__)
#endif

// print debug message
#ifndef NF2FS_DEBUG
#define NF2FS_DEBUG(fmt, ...) printf("%s:%d:debug: " fmt "\n", __FILE__, __LINE__)
#endif

// Runtime assertions
#ifndef NF2FS_ASSERT
#ifndef NF2FS_NO_ASSERT
#define NF2FS_ASSERT(test) assert(test)
#else
#define NF2FS_ASSERT(test)
#endif
#endif

// Min/max functions for unsigned 32-bit numbers
static inline uint32_t NF2FS_max(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

static inline uint32_t NF2FS_min(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

// Align to nearest multiple of a size
static inline uint32_t NF2FS_aligndown(uint32_t a, uint32_t alignment)
{
    return a - (a % alignment);
}

static inline uint32_t NF2FS_alignup(uint32_t a, uint32_t alignment)
{
    return NF2FS_aligndown(a + alignment - 1, alignment);
}

// Allocate memory, only used if buffers are not provided to NF2FS
static inline void* NF2FS_malloc(size_t size)
{
#ifndef NF2FS_NO_MALLOC
    // TODO, Need to change to the malloc function that used in your system
    // return pvPortMalloc(size);
    return malloc(size);
#else
    (void)size;
    return NULL;
#endif
}

// Deallocate memory, only used if buffers are not provided to NF2FS
static inline void NF2FS_free(void* p)
{
#ifndef NF2FS_NO_MALLOC
    // TODO, Need to add when all things is ready
    // vPortFree(p);
    return free(p);
#else
    (void)p;
#endif
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
// TODO, Need to add if it can not run in the future
// #endif
