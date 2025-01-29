/**
 * Simulatiton module, used to debug the NF2FS.
 */

#ifndef NF2FS_SIMULATE_H
#define NF2FS_SIMULATE_H

#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#ifdef __cplusplus
extern "C" {
#endif

#define W25Q80 0XEF13
#define W25Q16 0XEF14
#define W25Q32 0XEF15
#define W25Q64 0XEF16
#define W25Q128 0XEF17
#define W25Q256 0XEF18

#define W25Q256_ERASE_GRAN 4096
#define W25Q256_NUM_GRAN 8192

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -----------------------------------------------------------    Initialize function    ---------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

int W25QXX_init();

int W25QXX_free();

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------------    Operation port    ------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

int W25QXX_Read(void* buffer, int address, int size);

int W25QXX_Write_NoCheck(void* buffer, int address, int size);

int W25QXX_Erase_Sector(int sector);

void Erase_Times_Reset(void);

void Erase_Times_Print(char* name);

#ifdef __cplusplus
}
#endif

#endif