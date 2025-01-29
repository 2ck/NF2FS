/**
 * Big related related operations.
 */
#ifndef NF2FS_FILE_H
#define NF2FS_FILE_H

#include "NF2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

// free a file in file list
int NF2FS_file_free(NF2FS_file_ram_t** list, NF2FS_file_ram_t* file);

// prog function for big file data
int NF2FS_bfile_prog(NF2FS_t* NF2FS, NF2FS_size_t* sector, NF2FS_off_t* off, const void* buffer, NF2FS_size_t len);

// GC for parts of a very big file
int NF2FS_bfile_part_gc(NF2FS_t* NF2FS, NF2FS_file_ram_t* file, NF2FS_size_t start, NF2FS_size_t end, NF2FS_size_t len, NF2FS_size_t index_num, NF2FS_size_t sector_num);

// open file with file id.
int NF2FS_file_lowopen(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir, NF2FS_size_t id, NF2FS_size_t sector, NF2FS_off_t off, NF2FS_size_t namelen, NF2FS_file_ram_t** file_addr);

// Flush data in file cache to corresponding dir.
int NF2FS_file_flush(NF2FS_t* NF2FS, NF2FS_file_ram_t* file);

// create a new file
int NF2FS_create_file(NF2FS_t* NF2FS, NF2FS_dir_ram_t* dir, NF2FS_file_ram_t** file_addr, char* name, NF2FS_size_t namelen);

// read data of small file
int NF2FS_small_file_read(NF2FS_t* NF2FS, NF2FS_file_ram_t* file, void* buffer, NF2FS_size_t size);

// read data of big file
int NF2FS_big_file_read(NF2FS_t* NF2FS, NF2FS_file_ram_t* file, void* buffer, NF2FS_size_t size);

// write data to small file
int NF2FS_small_file_write(NF2FS_t* NF2FS, NF2FS_file_ram_t* file, const void* buffer, NF2FS_size_t size);

// writa data and change file from small to big.
int NF2FS_s2b_file_write(NF2FS_t* NF2FS, NF2FS_file_ram_t* file, const void* buffer, NF2FS_size_t size);

// set (begin, off, size) to (new_begin, new_off, size - jump_size)
void NF2FS_index_jump(NF2FS_t* NF2FS, NF2FS_bfile_index_ram_t* index, NF2FS_size_t jump_size);

// write data to big file
int NF2FS_big_file_write(NF2FS_t* NF2FS, NF2FS_file_ram_t* file, void* buffer, NF2FS_size_t size);

#ifdef __cplusplus
}
#endif

#endif
