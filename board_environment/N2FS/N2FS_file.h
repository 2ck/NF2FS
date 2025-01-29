/**
 * Big related related operations.
 */
#ifndef N2FS_FILE_H
#define N2FS_FILE_H

#include "N2FS.h"

#ifdef __cplusplus
extern "C" {
#endif

// free a file in file list
int N2FS_file_free(N2FS_file_ram_t** list, N2FS_file_ram_t* file);

// prog function for big file data
int N2FS_bfile_prog(N2FS_t* N2FS, N2FS_size_t* sector, N2FS_off_t* off, const void* buffer, N2FS_size_t len);

// GC for parts of a very big file
int N2FS_bfile_part_gc(N2FS_t* N2FS, N2FS_file_ram_t* file, N2FS_size_t start, N2FS_size_t end, N2FS_size_t len, N2FS_size_t index_num, N2FS_size_t sector_num);

// open file with file id.
int N2FS_file_lowopen(N2FS_t* N2FS, N2FS_dir_ram_t* dir, N2FS_size_t id, N2FS_size_t sector, N2FS_off_t off, N2FS_size_t namelen, N2FS_file_ram_t** file_addr);

// Flush data in file cache to corresponding dir.
int N2FS_file_flush(N2FS_t* N2FS, N2FS_file_ram_t* file);

// create a new file
int N2FS_create_file(N2FS_t* N2FS, N2FS_dir_ram_t* dir, N2FS_file_ram_t** file_addr, char* name, N2FS_size_t namelen);

// read data of small file
int N2FS_small_file_read(N2FS_t* N2FS, N2FS_file_ram_t* file, void* buffer, N2FS_size_t size);

// read data of big file
int N2FS_big_file_read(N2FS_t* N2FS, N2FS_file_ram_t* file, void* buffer, N2FS_size_t size);

// write data to small file
int N2FS_small_file_write(N2FS_t* N2FS, N2FS_file_ram_t* file, const void* buffer, N2FS_size_t size);

// writa data and change file from small to big.
int N2FS_s2b_file_write(N2FS_t* N2FS, N2FS_file_ram_t* file, const void* buffer, N2FS_size_t size);

// set (begin, off, size) to (new_begin, new_off, size - jump_size)
void N2FS_index_jump(N2FS_t* N2FS, N2FS_bfile_index_ram_t* index, N2FS_size_t jump_size);

// write data to big file
int N2FS_big_file_write(N2FS_t* N2FS, N2FS_file_ram_t* file, void* buffer, N2FS_size_t size);

#ifdef __cplusplus
}
#endif

#endif
