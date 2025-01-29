/**
 * Simulatiton module, used to debug the NF2FS.
 */

#include "nor_flash_simulate.h"
#include <stdio.h>

char *sflash = NULL;
int erase_times[8192] = {0};

// Init simulater
int W25QXX_init()
{
    int size = 4096 * 8192;

    // Allocate ram space
    sflash = malloc(size);
    if (sflash == NULL)
        return -1;
    memset(sflash, 0xffffffff, size);
    return 0;
}

// free ram space
int W25QXX_free()
{
    int err = 0;

    if (sflash)
        free(sflash);
    return -1;
}

// read data from simulater
int W25QXX_Read(void *buffer, int address, int size)
{
    char *data = sflash + address;
    memcpy(buffer, data, size);
    return 0;
}

// write data to simulater
int W25QXX_Write_NoCheck(void *buffer, int address, int size)
{
    char *data = sflash + address;
    char *src = (char *)buffer;
    while (size > 0) {
        *data &= *src;
        data++;
        src++;
        size--;
    }
    return 0;
}

// erase a sector
int W25QXX_Erase_Sector(int sector)
{
    if (sector >= 0 && sector < 8192) {
        erase_times[sector] += 1;
    } else {
        printf("erase sector is wrong!, %d\n", sector);
        return -1;
    }

    char *data = sflash + sector * 4096;
    memset(data, -1, 4096);
    return 0;
}

// reset erase times
void Erase_Times_Reset(void)
{
    memset(erase_times, 0, 8192 * 4);
}

// print erase times
void Erase_Times_Print(char *name)
{
    FILE *file;
    file = fopen(name, "w");
    if (file == NULL)
        printf("create erase file error\n");

    int cnt = 0;
    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 32; j++) {
            fprintf(file, "%3d ", erase_times[cnt]);
            cnt++;
        }
        fprintf(file, "\n");
    }

    fclose(file);
}