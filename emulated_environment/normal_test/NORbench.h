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

// test mount operations
void mount_test(const char *fsname);

// test basic operations for varing sizes files
void file_operations_test(const char *fsname, int loop);

// test file operations with dir layers
void dir_operations_test(const char *fsname, int dir_layer, int loop);

// test random write of the big file
void random_write_test(const char *fsname, int rwrite_times);

// test io for varing sizes files
void fs_io_test(const char *fsname);

// test busybox operations, including burn, read
void Busybox_test(const char *fsname);

// test the lifespan of nor flash
void wl_test(const char *fsname);

// test the gc performance of NF2FS
void gc_test(const char *fsname, int sfile_num, int rwrite_times);

// case study of data logging
void logging_test(const char *fsname, int log_size, int loop, int entry_num);

// raw operations for data logging
void raw_logging_test(int log_size, int loop, int entry_num);

// case study of ota update
void ota_test(const char *fsname, int file_size, int loop, int ota_times);

// case study of ota update
void raw_ota_test(int file_size, int loop);

// Test the inefficiency of multi-layer IO stack and device management
void IO_stack_test(const char *fsname, int sfile_num, int rwrite_times);

// test the performance of IO stack with multi-layer and device management
void raw_io_stack_test(int file_size, int sfile_num, int rwrite_times);

// we should simulate for each configurations
// test the effectiveness of allocating and wear leveling strategies.
void device_management_logging(int log_size, int entry_num);

#endif /* __BENCHMARK_H */
