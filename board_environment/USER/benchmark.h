// Copyright (C) 2022 Deadpool
//
// Nor Flash File System Benchmark
//
// NORENV is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// NORENV is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with NORENV.  If not, see <http://www.gnu.org/licenses/>.

#ifndef __BENCHMARK_H
#define __BENCHMARK_H

// test io for varing sizes files
void fs_io_test(const char *fsname);

// test busybox operations, including burn, read
void Busybox_test(const char *fsname);

// test the lifespan of nor flash
void wl_test(const char *fsname);

// test the gc performance of N2FS
void gc_test(const char *fsname, int sfile_num, int rwrite_times);

// case study of data logging
void logging_test(const char *fsname, int log_size, int loop, int entry_num);

// raw operations for data logging
void raw_logging_test(int log_size, int loop, int entry_num);

// case study of ota update
void ota_test(const char *fsname, int file_size, int loop, int ota_times);

// Real world workloads-ota update
void overwrite_ota_test(const char *fsname, int file_size, int loop, int ota_times);

// case study of ota update
void raw_ota_test(int file_size, int loop, int ota_times);

// mount test
void mount_test(const char *fsname);

// test the operations of files and directories
void operation_test(const char *fsname, int sfile_num);

// Test the inefficiency of multi-layer IO stack and device management
void IO_stack_test(const char *fsname, int sfile_num, int rwrite_times);

void create_io_stack_test(const char *fsname, int sfile_num);

// test the performance of IO stack with multi-layer and device management
void create_raw_io_stack_test(int sfile_num);

// test the performance of IO stack with device management
void dmanag_io_stack_test(int file_size, int sfile_num, int rwrite_times);

// frequent read headers of each sector
void heads_read_test(int read_size, int loop);

// test the effectiveness of allocating and wear leveling strategies.
void device_management_logging(int log_size, int entry_num);

// test the effectiveness of allocating and wear leveling strategies.
void device_management_ota(void);

// test the ideal performance of all operations
void ideal_io_test(void);

// Total time used by dual bitmap and allocation
void dual_bitmap_io_time(int file_size, int rwrite_times);

#endif /* __BENCHMARK_H */
