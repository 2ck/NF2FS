#include "nfvfs.h"
#include "lfs.h"
#include "delay.h"
#include "FreeRTOS.h"
#include "task.h"
#include "w25qxx.h"
#include "benchmark.h"
#include "N2FS_rw.h"

#include "ff.h"
#include "exfuns.h"
#include <stdint.h>

// It's used to checkout the number of files in busybox
int cnt = 0;

uint32_t total_time = 0;
uint32_t total_mount = 0;
uint32_t total_unmount = 0;
uint32_t total_create = 0;
uint32_t total_open = 0;
uint32_t total_write = 0;
uint32_t total_read = 0;
uint32_t total_close = 0;
uint32_t total_lseek = 0;
uint32_t total_readdir = 0;
uint32_t total_fssize = 0;
uint32_t total_delete = 0;

void total_time_reset()
{
  total_time = 0;
  total_mount = 0;
  total_unmount = 0;
  total_create = 0;
  total_open = 0;
  total_write = 0;
  total_read = 0;
  total_close = 0;
  total_lseek = 0;
  total_readdir = 0;
  total_fssize = 0;
  total_delete = 0;
}

void total_time_print()
{
  printf("\r\n\r\n\r\n");
  printf("total time is         	%u\r\n", total_time);
  printf("total mount time is   	%u\r\n", total_mount);
  printf("total unmount time is 	%u\r\n", total_unmount);
  printf("total create time is    %u\r\n", total_create);
  printf("total open time is    	%u\r\n", total_open);
  printf("total write time is   	%u\r\n", total_write);
  printf("total read time is    	%u\r\n", total_read);
  printf("total close time is   	%u\r\n", total_close);
  printf("total lseek time is   	%u\r\n", total_lseek);
  printf("total readdir time is   %u\r\n", total_readdir);
  printf("total fs size time is   %u\r\n", total_fssize);
  printf("total delete time is    %u\r\n", total_delete);
  printf("\r\n\r\n\r\n");
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------------    Basic IO Operations    --------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

void raw_mount(struct nfvfs *fs)
{
  nfvfs_mount(fs);
  return;
}

int raw_open(struct nfvfs *fs, char *path, int flags, int type)
{
  int fd = nfvfs_open(fs, path, flags, type);
  if (fd < 0) {
    printf("open file failed: %d\r\n", fd);
    assert(-1 > 0);
  }
  return fd;
}

void raw_write(struct nfvfs *fs, int fd, int size)
{
  char test_buffer[size > 4096 ? 4096 : size];
  memset(test_buffer, 0, size > 4096 ? 4096 : size);

  while (size > 0) {
    int min = (size > 4096) ? 4096 : size;
    int ret = nfvfs_write(fs, fd, test_buffer, min);
    if (ret < 0) {
      printf("write file failed: %d\r\n", ret);
      assert(-1 > 0);
    }
    size -= min;
  }
  return;
}

void raw_lseek(struct nfvfs *fs, int fd, int off)
{
  int ret = nfvfs_lseek(fs, fd, off, NFVFS_SEEK_SET);
  if (ret < 0) {
    printf("lseek file failed: %d\r\n", ret);
    assert(-1 > 0);
  }
  return;
}

void raw_read(struct nfvfs *fs, int fd, int size)
{
  char test_buffer[size > 4096 ? 4096 : size];
  memset(test_buffer, 0, size > 4096 ? 4096 : size);

  while (size > 0) {
    int min = (size > 4096) ? 4096 : size;
    int ret = nfvfs_read(fs, fd, test_buffer, min);
    if (ret < 0) {
      printf("read file failed: %d\r\n", ret);
      assert(-1 > 0);
    }
    size -= min;
  }

  return;
}

void raw_close(struct nfvfs *fs, int fd)
{
  int err = nfvfs_close(fs, fd);
  if (err < 0) {
    printf("close err, err is %d\r\n", err);
  }
  return;
}

void raw_unmount(struct nfvfs *fs)
{
  int err = nfvfs_umount(fs);
  if (err < 0) {
    printf("unmount err, err is %d\r\n", err);
  }
  return;
}

void raw_fssize(struct nfvfs *fs)
{
  int fs_size = nfvfs_fssize(fs);
}

void raw_readdir(struct nfvfs *fs, int fd, struct nfvfs_dentry *info)
{
  int err = 0;
  err = nfvfs_readdir(fs, fd, info);
  if (err < 0) {
    printf("read dir entry error, error is %d\r\n", err);
  }
}

void raw_delete(struct nfvfs *fs, int fd, char *path, int mode)
{
  int err = 0;
  err = nfvfs_remove(fs, fd, path, mode);
  if (err < 0) {
    printf("remove path %s error, error is %d\r\n", path, err);
  }
}

// the basic test module for a file, including open, read, write, close
void raw_basic_test(struct nfvfs *fs, char *path, int len, int loop)
{
  int fd;
  fd = raw_open(fs, path, O_RDWR | O_CREAT, S_ISREG);

  uint32_t start = (uint32_t)xTaskGetTickCount();
  for (int i = 0; i < loop; i++) {
    raw_lseek(fs, fd, 0);
    raw_write(fs, fd, len);
    nfvfs_fsync(fs, fd);
  }
  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("write time is %d\r\n", end - start);

  start = (uint32_t)xTaskGetTickCount();
  for (int i = 0; i < loop; i++) {
    raw_lseek(fs, fd, 0);
    raw_read(fs, fd, len);
  }
  end = (uint32_t)xTaskGetTickCount();
  printf("read time is %u\r\n", end - start);

  raw_close(fs, fd);
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------------    Busybox test    --------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

FRESULT write_busybox_test(
    char *path, /* Start node to be scanned (***also used as work area***) */
    struct nfvfs *dst_fs,
    const char *fsname)
{
  FRESULT res;
  DIR dir;
  UINT i;
  static FILINFO fno;
  uint32_t start, end;

  char dst_path[256];
  memset(dst_path, 0, 256);
  strcpy(dst_path, &path[2]);

  int dst_i = strlen(dst_path);

  // create new dir
  start = (uint32_t)xTaskGetTickCount();
  int fd = raw_open(dst_fs, dst_path, LFS_O_CREAT, S_ISDIR);
  end = (uint32_t)xTaskGetTickCount();
  total_create += end - start; 

  res = f_opendir(&dir, path); /* Open the directory */
  if (res == FR_OK) {
    for (;;) {
      res = f_readdir(&dir, &fno); /* Read a directory item */
      if (res != FR_OK || fno.fname[0] == 0)
        break; /* Break on error or end of dir */
      if (fno.fattrib & AM_DIR) { 
        /* It is a directory */
        i = strlen(path);
        sprintf(&path[i], "/%s", fno.fname);
        res = write_busybox_test(path, dst_fs, fsname); /* Enter the directory */
        if (res != FR_OK)
          break;
        memset(&path[i], '\0', 255 - i);
      } else { /* It is a file. */
        sprintf(&dst_path[dst_i], "/%s", fno.fname);

        // create new file
        start = (uint32_t)xTaskGetTickCount();
        int fd2 = raw_open(dst_fs, dst_path, O_RDWR | O_CREAT, S_ISREG);
        end = (uint32_t)xTaskGetTickCount();
        total_create += end - start; 

        // Debug
        // printf("fno.fisze is %u\r\n", (uint32_t)fno.fsize);

        // write do not need to cal
        start = (uint32_t)xTaskGetTickCount();
        raw_write(dst_fs, fd2, (uint32_t)fno.fsize);
        end = (uint32_t)xTaskGetTickCount();
        total_write += end - start; 

        // close the file
        start = (uint32_t)xTaskGetTickCount();
        raw_close(dst_fs, fd2);
        end = (uint32_t)xTaskGetTickCount();
        total_close += end - start; 
      }
    }
    f_closedir(&dir);
  }

  // close the dir
  start = (uint32_t)xTaskGetTickCount();
  raw_close(dst_fs, fd);
  end = (uint32_t)xTaskGetTickCount();
  total_close += end - start; 
  return res;
}

int read_busybox_test(char *path, struct nfvfs *dst_fs)
{
  int err = 0;
  int my_size = 2675216;
  uint32_t start, end;

  // open the file
  start = (uint32_t)xTaskGetTickCount();
  int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
  end = (uint32_t)xTaskGetTickCount();
  total_open += end - start; 

  // read busybox
  start = (uint32_t)xTaskGetTickCount();
  raw_lseek(dst_fs, fd, 0);
  nfvfs_fsync(dst_fs, fd);
  end = (uint32_t)xTaskGetTickCount();
  total_lseek += end - start; 

  start = (uint32_t)xTaskGetTickCount();
  raw_read(dst_fs, fd, my_size);
  end = (uint32_t)xTaskGetTickCount();
  total_read += end - start; 
  
  // close the file
  start = (uint32_t)xTaskGetTickCount();
  raw_close(dst_fs, fd);
  end = (uint32_t)xTaskGetTickCount();
  total_close += end - start; 

  return err;
}

int traverse_busybox_test(char *path, struct nfvfs *dst_fs)
{
  int err = 0;
  uint32_t start, end;

  // open the file
  start = (uint32_t)xTaskGetTickCount();
  int fd = nfvfs_open(dst_fs, path, O_APPEND, S_ISDIR);
  if (fd < 0) {
    printf("open dir error: %s, %d\r\n", path, fd);
    return err;
  }
  end = (uint32_t)xTaskGetTickCount();
  total_open += end - start; 

  struct nfvfs_dentry info;
  info.name = pvPortMalloc(256);
  if (info.name == NULL) {
    printf("There is no enough memory\r\n");
    return LFS_ERR_NOMEM;
  }

  while (true) {
    // read a dir entry
    start = (uint32_t)xTaskGetTickCount();
    raw_readdir(dst_fs, fd, &info);
    end = (uint32_t)xTaskGetTickCount();
    total_readdir += end - start; 

    if (info.type == NFVFS_TYPE_REG) {
      cnt++;
    } else if (info.type == NFVFS_TYPE_DIR) {
      if (info.name[0] == '.')
        continue;

      char temp[256];
      memset(temp, 0, 256);
      strcpy(temp, path);
      int len = strlen(temp);
      temp[len] = '/';
      strcpy(&temp[len + 1], info.name);

      // // NEXT
      // printf("address is %s, %s\r\n", path, temp);

      err = traverse_busybox_test(temp, dst_fs);
      if (err) {
        goto cleanup;
      }
    } else if (info.type == NFVFS_TYPE_END) {
      printf("file number is %d\r\n", cnt);
      goto cleanup;
    } else {
      printf("something is error in read busybox, %d\r\n", info.type);
      err = -1;
      goto cleanup;
    }
  }

cleanup:
  // close the file
  // start = (uint32_t)xTaskGetTickCount();
  // raw_close(dst_fs, fd);
  // end = (uint32_t)xTaskGetTickCount();
  // total_close += end - start; 

  vPortFree(info.name);
  return err;
}

// part of the operation test
void Busybox_test(const char *fsname)
{
  uint32_t temp_start, temp_end;
  uint32_t start = (uint32_t)xTaskGetTickCount();

  // get and mount file system 
  printf("-----------------Busybox test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }

  char buff[256];
  FRESULT res = f_mount(fs[2], "2:", 1);
  if (res == 0X0D) {
    printf("Flash Disk Formatting...\r\n");
    res = f_mkfs("1:", FM_ANY, 0, fatbuf, FF_MAX_SS);
    if (res == 0) {
      f_setlabel((const TCHAR *)"1:ALIENTEK");
      printf("Flash Disk Formatting...\r\n");
    }
    else
      printf("Flash Disk Format Error \r\n");
    delay_ms(1000);
  } else if (res == FR_OK) {
    printf("fatfs mount success!\r\n");
  }

  raw_mount(dst_fs);

  // write busybox
  printf("\r\n\r\n-----------------Write Busybox-----------------\r\n\r\n");
  strcpy(buff, "2:/busybox");
  total_time_reset();
  temp_start = (uint32_t)xTaskGetTickCount();
  res = write_busybox_test(buff, dst_fs, fsname);
  temp_end = (uint32_t)xTaskGetTickCount();
  printf("total time is %u\r\n", temp_end - temp_start);
  total_time_print();

  printf("\r\n\r\n-----------------Traverse Busybox-----------------\r\n\r\n");
  memset(buff, 0, 256);
  strcpy(buff, "/busybox");
  total_time_reset();
  temp_start = (uint32_t)xTaskGetTickCount();
  traverse_busybox_test(buff, dst_fs);
  temp_end = (uint32_t)xTaskGetTickCount();
  printf("total time is %u\r\n", temp_end - temp_start);
  total_time_print();


  // printf("\r\n\r\n-----------------Read Busybox-----------------\r\n\r\n");
  // memset(buff, 0, 256);
  // strcpy(buff, "/busybox/bin/busybox");
  // total_time_reset();
  // temp_start = (uint32_t)xTaskGetTickCount();
  // int my_res = read_busybox_test(buff, dst_fs);
  // if (my_res < 0) {
  //   printf("update error,%d\r\n", my_res);
  // }
  // temp_end = (uint32_t)xTaskGetTickCount();
  // printf("total time is %u\r\n", temp_end - temp_start);
  // total_time_print();

  raw_unmount(dst_fs);

  // print basic message
  printf("\r\n\r\n");
  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("Total project time is %d, %d, %d\r\n", end - start, start, end);
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * ----------------------------------------------------------    New operation test    -----------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// IO performance test
void fs_io_test(const char *fsname)
{
  struct nfvfs *fs;

  // get and mount file system
  printf("-----------------begin-----------------\r\n");
  fs = get_nfvfs(fsname);
  if (!fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }

  uint32_t start = (uint32_t)xTaskGetTickCount();
  raw_mount(fs);
  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("Mount time is %d\r\n", end - start);

  start = (uint32_t)xTaskGetTickCount();
  raw_fssize(fs);
  end = (uint32_t)xTaskGetTickCount();
  printf("SPIFFS Check time is %d\r\n", end - start);

  // init file
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "abcd");
  int tail = strlen(path) - 1;
  char my_cnt = 'A';

  // test small file io 
  printf("-----------------small file io test-----------------\r\n");
  int size = 2;
  for (int i = 0; i < 10; i++) {
    path[tail] = my_cnt++;
    if (size <= 256)
      raw_basic_test(fs, path, size, 100);
    else
      raw_basic_test(fs, path, size, 1);
    size = size * 2;
  }

  // test big file sequential io
  printf("-----------------big file sequential test-----------------\r\n"); 
  size = 2 * 1024;
  for (int i = 0; i < 10; i++) {
    path[tail] = my_cnt++;
    raw_basic_test(fs, path, size, 1);
    size = size + 2 * 1024;
  }

  // test big file random io 
  size = 2 * 1024 * 1024;
  path[tail] = my_cnt++;
  int fd= raw_open(fs, path, O_RDWR | O_CREAT, S_ISREG);
  raw_write(fs, fd, size);
  printf("-----------------big file random read test-----------------\r\n\r\n");
  // for (int i = 1; i <= 20; i++) {
  //   uint32_t start = (uint32_t)xTaskGetTickCount();
  //   for (int j = 0; j < i; j++) {
  //     raw_lseek(fs, fd, rand() % (2 * 1024 * 1024 - 1024));
  //     raw_read(fs, fd, 1024);
  //   }
  //   uint32_t end = (uint32_t)xTaskGetTickCount();
  //   printf("big file random read time is %u\r\n", end - start);
  // }

  int test_size = 20 * 1024;
  start = (uint32_t)xTaskGetTickCount();
  while (test_size > 0) {
    int min = (test_size > 1024) ? 1024 : test_size;
    raw_lseek(fs, fd, rand() % (2 * 1024 * 1024));
    // raw_lseek(fs, fd, 0);
    raw_read(fs, fd, min);
    test_size -= min;
    uint32_t end = (uint32_t)xTaskGetTickCount();
    printf("big file random read time is %u\r\n", end - start);
  }

  printf("-----------------big file random write test-----------------\r\n\r\n");
  test_size = 20 * 1024;
  start = (uint32_t)xTaskGetTickCount();
  while (test_size > 0) {
    int min = (test_size > 1024) ? 1024 : test_size;
    raw_lseek(fs, fd, rand() % (2 * 1024 * 1024));
    raw_write(fs, fd, min);
    test_size -= min;
    uint32_t end = (uint32_t)xTaskGetTickCount();
    printf("big file random write time is %u\r\n", end - start);
  }
  raw_close(fs, fd);

  // umount file
  printf("\r\n\r\n\r\n");
  raw_unmount(fs);
  printf("-----------------end-----------------\r\n");
}

// test the lifespan of nor flash
// TODO, need change in the future
void wl_test(const char *fsname)
{
  // get and mount file system
  printf("-----------------wl test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs)
  {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }
  raw_mount(dst_fs);

  // file path
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/zzzz");

  // write a big file
  printf("-----------------write big file-----------------\r\n\r\n");
  int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
  int file_size = 16 * 1024 * 1024;
  raw_write(dst_fs, fd, file_size);
  raw_close(dst_fs, fd);

  // write small files
  printf("-----------------write small files-----------------\r\n\r\n");
  int tail = strlen(path);
  for (int j = 0; j < 1000; j++) {
    printf("loop %d\n", j);

    // write i files
    path[tail - 1] = 'A';
    path[tail - 2] = 'A';
    path[tail - 3] = 'A';
    for (int i = 0; i < 16 * 1024; i++) {
      int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
      raw_write(dst_fs, fd, 32);
      raw_close(dst_fs, fd);

      path[tail - 1] += 1;
      if (path[tail - 1] == 'z') {
        path[tail - 1] = 'A';
        path[tail - 2] += 1;
        if (path[tail - 2] == 'z') {
          path[tail - 2] = 'A';
          path[tail - 3] += 1;
        }
      }
    }

    // delete i files
    path[tail - 1] = 'A';
    path[tail - 2] = 'A';
    path[tail - 3] = 'A';
    for (int i = 0; i < 16 * 1024; i++) {
      raw_delete(dst_fs, fd, path, S_ISDIR);
      path[tail - 1] += 1;
      if (path[tail - 1] == 'z') {
        path[tail - 1] = 'A';
        path[tail - 2] += 1;
        if (path[tail - 2] == 'z') {
          path[tail - 2] = 'A';
          path[tail - 3] += 1;
        }
      }
    }
  }

  // Unmount fs
  raw_unmount(dst_fs);
  printf("-----------------wl test end-----------------\r\n\r\n");
}

// test the gc performance of N2FS
void gc_test(const char *fsname, int sfile_num, int rwrite_times)
{
  uint32_t temp_start, temp_end;
  total_time_reset();

  // Get and mount file system
  printf("-----------------gc test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }

  temp_start= (uint32_t)xTaskGetTickCount();
  raw_mount(dst_fs);
  temp_end = (uint32_t)xTaskGetTickCount();
  total_mount+= temp_end - temp_start;

  // init file path
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/test1.?");
  int tail = strlen(path);
  char my_cnt1 = 'A';
  char my_cnt2 = 'A';

  // small file gc
  printf("-----------------dir gc-----------------\r\n\r\n");
  nor_flash_message_reset();
  // in_place_size_reset();
  uint32_t start = (uint32_t)xTaskGetTickCount();
  for (int i = 0; i < sfile_num; i++) {
    // create new small files
    path[tail - 1] = my_cnt1;
    path[tail] = my_cnt2;

    temp_start= (uint32_t)xTaskGetTickCount();
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_create+= temp_end - temp_start;

    temp_start= (uint32_t)xTaskGetTickCount();
    raw_write(dst_fs, fd, 32);
    nfvfs_fsync(dst_fs, fd);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_write+= temp_end - temp_start;

    my_cnt1++;
    if (my_cnt1 == 'z') {
      my_cnt1 = 'A';
      my_cnt2 += 1;
    }

    if (i % 2 == 0) {
      temp_start= (uint32_t)xTaskGetTickCount();
      raw_close(dst_fs, fd);
      temp_end = (uint32_t)xTaskGetTickCount();
      total_close+= temp_end - temp_start;
    } else {
      temp_start= (uint32_t)xTaskGetTickCount();
      nfvfs_fsync(dst_fs, fd);
      if (fsname[0] == 'l' && fsname[1] == 'i')
        raw_close(dst_fs, fd);
      raw_delete(dst_fs, fd, path, S_ISREG);
      temp_end = (uint32_t)xTaskGetTickCount();
      total_delete+= temp_end - temp_start;
    }

    if (i % 10 == 9) {
      // GC one block for spiffs, not for other file systems
      temp_start= (uint32_t)xTaskGetTickCount();
      nfvfs_gc(dst_fs, 0);
      temp_end = (uint32_t)xTaskGetTickCount();
      printf("only gc time is %u\r\n", temp_end - temp_start);
    }

    if (i % 500 == 499) {
      if (fsname[0] == 's' && fsname[1] == 'p' && i == 1999) {
        temp_start= (uint32_t)xTaskGetTickCount();
        nfvfs_gc(dst_fs, 0);
        temp_end = (uint32_t)xTaskGetTickCount();
        printf("only gc time is %u\r\n", temp_end - temp_start);
      }

      if (fsname[0] == 'l' && fsname[1] == 'i')
        nfvfs_gc(dst_fs, 0);
      uint32_t end = (uint32_t)xTaskGetTickCount();
      printf("dir gc time is %u\r\n", end - start);
    }
  }
  nor_flash_message_print();
  total_time_print();
  // in_place_size_print();

  nor_flash_message_reset();
  total_time_reset();
  // in_place_size_reset();
  // big file gc begin
  printf("-----------------big file gc-----------------\r\n\r\n");
  for (int i = 0; i < 1; i++) {
    // create a big file
    path[tail - 1] = my_cnt1;
    path[tail] = my_cnt2;

    temp_start= (uint32_t)xTaskGetTickCount();
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_create+= temp_end - temp_start;

    my_cnt1++;
    if (my_cnt1 == 'z') {
      my_cnt1 = 'A';
      my_cnt2 += 1;
    }
    int file_size = 2 * 1024 * 1024;

    temp_start= (uint32_t)xTaskGetTickCount();
    raw_write(dst_fs, fd, file_size);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_write+= temp_end - temp_start;

    // random write and gc
    start = (uint32_t)xTaskGetTickCount();
    for (int i= 0; i < rwrite_times; i++) {
      int temp = rand() % file_size;

      temp_start= (uint32_t)xTaskGetTickCount();
      raw_lseek(dst_fs, fd, temp);
      raw_write(dst_fs, fd, 1024);
      nfvfs_fsync(dst_fs, fd);
      temp_end = (uint32_t)xTaskGetTickCount();
      total_write+= temp_end - temp_start;

      if (i % 10 == 9) {
        if (fsname[0] == 's' && fsname[1] == 'p' && i == 39) {
          temp_start= (uint32_t)xTaskGetTickCount();
          nfvfs_gc(dst_fs, 0);
          temp_end = (uint32_t)xTaskGetTickCount();
          printf("only gc time is %u\r\n", temp_end - temp_start);
        }
        uint32_t end = (uint32_t)xTaskGetTickCount();
        printf("big file gc time is %u\r\n", end - start);
      }
    }
    temp_start= (uint32_t)xTaskGetTickCount();
    raw_close(dst_fs, fd);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_close+= temp_end - temp_start;
  }
  nor_flash_message_print();

  temp_start= (uint32_t)xTaskGetTickCount();
  raw_unmount(dst_fs);
  temp_end = (uint32_t)xTaskGetTickCount();
  total_unmount+= temp_end - temp_start;
  total_time_print();
  // in_place_size_print();
  printf("-----------------gc test end-----------------\r\n\r\n");
}

// Real world workloads-logging
void logging_test(const char *fsname, int log_size, int loop, int entry_num)
{
  // Get and mount file system
  printf("-----------------logging test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }
  raw_mount(dst_fs);

  // init file path
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/test1.?");
  int tail = strlen(path);
  char my_cnt1 = 'A';

  // test logging
  for (int i= 0; i < loop; i++) {
    path[tail - 1] = my_cnt1;
    my_cnt1++;

    nor_flash_message_reset();
    uint32_t start = (uint32_t)xTaskGetTickCount();
    printf("-----------------logging size %d-----------------\r\n\r\n", log_size);
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    for (int j= 0; j < entry_num; j++) {
      raw_write(dst_fs, fd, log_size);
    }
    raw_close(dst_fs, fd);
    log_size= log_size * 2;
    uint32_t end = (uint32_t)xTaskGetTickCount();
    printf("logging test time is %u\r\n", end - start);
    nor_flash_message_print();
  }

  raw_unmount(dst_fs);
  printf("-----------------logging test end-----------------\r\n\r\n");
}

// raw operations for data logging
void raw_logging_test(int log_size, int loop, int entry_num)
{
  printf("-----------------raw logging test begin-----------------\r\n\r\n");

  int super_off= 0;
  int log_off= 8192;
  char buff[4096];

  for (int i= 0; i < loop; i++) {
    printf("-----------------logging size %d-----------------\r\n\r\n", log_size);
    uint32_t start = (uint32_t)xTaskGetTickCount();
    // init for current sector + crc
    super_off= 0;
    log_off= 8192;
    W25QXX_Write_NoCheck((uint8_t *)buff, super_off, 8);
    super_off+= 8;
    
    // write log entry
    for (int j= 0; j < entry_num; j++) {
      // current sector can not store new log
      if ((4096 - (log_off % 4096)) < (log_size + 4)) {
        log_off+= 4096 - log_off % 4096;
        W25QXX_Write_NoCheck((uint8_t *)buff, super_off, 8);
        super_off+= 8;
        if (super_off >= 4096) {
          W25QXX_Erase_Sector(0);
          super_off= 0;
        }
      }

      // write new log
      W25QXX_Write_NoCheck((uint8_t *)buff, log_off, log_size + 4);
      log_off+= log_size + 4;
      assert(log_off % 4096 != 0);
    }
    log_size= log_size * 2;
    uint32_t end = (uint32_t)xTaskGetTickCount();
    printf("logging test time is %u\r\n", end - start);
  }

  printf("-----------------raw logging test end-----------------\r\n\r\n");
}

// Real world workloads-ota update
void ota_test(const char *fsname, int file_size, int loop, int ota_times)
{
  // Get and mount file system
  printf("-----------------ota test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }
  raw_mount(dst_fs);

  // init file path
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/test1.?");
  int tail = strlen(path);
  char my_cnt1 = 'A';

  // test ota
  for (int i= 0; i < loop; i++) {
    path[tail - 1] = my_cnt1;
    my_cnt1++;

    // write metadata
    uint32_t my_time_cnt = 0;
    uint32_t start = (uint32_t)xTaskGetTickCount();
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    int metadata_size = file_size / 4096 * 128 / 4096 + 1;
    raw_write(dst_fs, fd, metadata_size);
    raw_close(dst_fs, fd);
    path[tail - 1] = my_cnt1;
    my_cnt1++;
    uint32_t end = (uint32_t)xTaskGetTickCount();
    my_time_cnt += end - start;

    printf("-----------------file_size %d-----------------\r\n\r\n", file_size);
    fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_write(dst_fs, fd, file_size);
    raw_lseek(dst_fs, fd, 0);
    raw_read(dst_fs, fd, file_size);
    // nor_flash_message_reset();
    
    start = (uint32_t)xTaskGetTickCount();
    for (int j = 0; j < ota_times; j++) {
      // printf("times %d\r\n", j);
      raw_lseek(dst_fs, fd, rand() % file_size);
      raw_write(dst_fs, fd, 4096);
    }
    raw_close(dst_fs, fd);
    file_size= file_size * 2;
    end = (uint32_t)xTaskGetTickCount();
    my_time_cnt += end - start;
    printf("ota test time is %u\r\n", my_time_cnt);
    // nor_flash_message_print();
  }

  raw_unmount(dst_fs);
  printf("-----------------ota test end-----------------\r\n\r\n");
}

// Real world workloads-ota update
void overwrite_ota_test(const char *fsname, int file_size, int loop, int ota_times)
{
  // Get and mount file system
  printf("-----------------ota test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }
  raw_mount(dst_fs);

  // init file path
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/test1.?");
  int tail = strlen(path);
  char my_cnt1 = 'A';

  // test ota
  for (int i= 0; i < loop; i++) {
    path[tail - 1] = my_cnt1;
    my_cnt1++;

    // write metadata
    uint32_t my_time_cnt = 0;
    uint32_t start = (uint32_t)xTaskGetTickCount();
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    int metadata_size = file_size / 4096 * 128 / 4096 + 1;
    raw_write(dst_fs, fd, metadata_size);
    raw_close(dst_fs, fd);
    path[tail - 1] = my_cnt1;
    my_cnt1++;
    uint32_t end = (uint32_t)xTaskGetTickCount();
    my_time_cnt += end - start;

    printf("-----------------file_size %d-----------------\r\n\r\n", file_size);
    fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_lseek(dst_fs, fd, 0);
    raw_write(dst_fs, fd, file_size);
    // nor_flash_message_reset();
    raw_close(dst_fs, fd);

    start = (uint32_t)xTaskGetTickCount();
    fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_read(dst_fs, fd, file_size);
    raw_delete(dst_fs, fd, path, S_ISREG);
    fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_lseek(dst_fs, fd, 0);
    raw_write(dst_fs, fd, file_size);
    raw_close(dst_fs, fd);
    file_size= file_size * 2;
    end = (uint32_t)xTaskGetTickCount();

    my_time_cnt += end - start;
    printf("ota test time is %u\r\n", my_time_cnt);
    // nor_flash_message_print();
  }

  raw_unmount(dst_fs);
  printf("-----------------ota test end-----------------\r\n\r\n");
}


// case study of ota update
void raw_ota_test(int file_size, int loop, int ota_times)
{
  printf("-----------------raw ota test begin-----------------\r\n\r\n");

  char buff[4096];

  for (int i= 0; i < loop; i++) {
    uint32_t start = (uint32_t)xTaskGetTickCount();

    // 32B sha256 per 4~KB block + other metadata
    // others are for additional data transfer time
    int metadata_size = file_size / 4096 * 128 / 4096 + 1;
    for (int j = 0; j < metadata_size; j++) {
      W25QXX_Write_NoCheck((uint8_t *)buff, 4 * 4096, 4096);
    }

    printf("-----------------ota size %d-----------------\r\n\r\n", file_size);
    int rest_size= file_size;
    int address= 4 * 4096;
    while (rest_size != 0) {
      W25QXX_Read((uint8_t *)buff, address, 4096);
      W25QXX_Write_NoCheck((uint8_t *)buff, address, 4096);
      W25QXX_Erase_Sector(address / 4096);
      address+= 4096;
      rest_size-= 4096;
    }
    file_size = file_size * 2;
    
    uint32_t end = (uint32_t)xTaskGetTickCount();
    printf("ota test time is %u\r\n", end - start);
  }
  
  printf("-----------------raw ota test end-----------------\r\n\r\n");
}

// mount test
void mount_test(const char *fsname)
{
  uint32_t start, end;

  // Get and mount file system
  printf("-----------------mount test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }

  for (int i = 0; i < 5; i++) {
    start = (uint32_t)xTaskGetTickCount();
    raw_mount(dst_fs);
    end = (uint32_t)xTaskGetTickCount();
    printf("mount time is %u\r\n", end - start);

    start = (uint32_t)xTaskGetTickCount();
    raw_unmount(dst_fs);
    end = (uint32_t)xTaskGetTickCount();
    printf("unmount time is %u\r\n", end - start);
  }
}

// Operation test
void operation_test(const char *fsname, int sfile_num)
{
  uint32_t temp_start, temp_end, start, end;

  // Get and mount file system
  printf("-----------------small file test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }

  raw_mount(dst_fs);

  // init file path
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/test1.?");
  int tail = strlen(path);
  char my_cnt1 = 'A';
  char my_cnt2 = 'A';

  // small file test
  printf("-----------------prog small file-----------------\r\n\r\n");
  for (int i = 0; i < sfile_num; i++) {
    // create new small files
    path[tail - 1] = my_cnt1;
    path[tail] = my_cnt2;

    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_write(dst_fs, fd, 32);
    raw_close(dst_fs, fd);

    my_cnt1++;
    if (my_cnt1 == 'z') {
      my_cnt1 = 'A';
      my_cnt2 += 1;
    }
  }

  printf("-----------------read small file-----------------\r\n\r\n");
  total_time_reset();
  memset(path, 0, 64);
  strcpy(path, "/test1.?");
  tail = strlen(path);
  my_cnt1 = 'A';
  my_cnt2 = 'A';
  start= (uint32_t)xTaskGetTickCount();
  for (int i = 0; i < sfile_num; i++) {
    // create new small files
    path[tail - 1] = my_cnt1;
    path[tail] = my_cnt2;

    temp_start= (uint32_t)xTaskGetTickCount();
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_open+= temp_end - temp_start;

    temp_start= (uint32_t)xTaskGetTickCount();
    raw_read(dst_fs, fd, 32);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_read+= temp_end - temp_start;

    temp_start= (uint32_t)xTaskGetTickCount();
    raw_close(dst_fs, fd);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_close+= temp_end - temp_start;

    my_cnt1++;
    if (my_cnt1 == 'z') {
      my_cnt1 = 'A';
      my_cnt2 += 1;
    }
  }
  end = (uint32_t)xTaskGetTickCount();
  total_time_print();
  printf("total read time is %u\r\n", end - start);

  printf("-----------------delete small file-----------------\r\n\r\n");
  total_time_reset();
  memset(path, 0, 64);
  strcpy(path, "/test1.?");
  tail = strlen(path);
  my_cnt1 = 'A';
  my_cnt2 = 'A';
  start= (uint32_t)xTaskGetTickCount();
  for (int i = 0; i < sfile_num; i++) {
    // create new small files
    path[tail - 1] = my_cnt1;
    path[tail] = my_cnt2;

    if (fsname[0] == 'l') {
      temp_start= (uint32_t)xTaskGetTickCount();
      raw_delete(dst_fs, 0, path, S_ISREG);
      temp_end = (uint32_t)xTaskGetTickCount();
      total_delete+= temp_end - temp_start;
    } else {
      temp_start= (uint32_t)xTaskGetTickCount();
      int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
      temp_end = (uint32_t)xTaskGetTickCount();
      total_open+= temp_end - temp_start;

      temp_start= (uint32_t)xTaskGetTickCount();
      raw_delete(dst_fs, fd, path, S_ISREG);
      temp_end = (uint32_t)xTaskGetTickCount();
      total_delete+= temp_end - temp_start;
    }

    my_cnt1++;
    if (my_cnt1 == 'z') {
      my_cnt1 = 'A';
      my_cnt2 += 1;
    }
  }
  end = (uint32_t)xTaskGetTickCount();
  total_time_print();
  printf("total delete time is %u\r\n", end - start);

  printf("-----------------prog big file-----------------\r\n\r\n");
  for (int i = 0; i < 1; i++) {
    // create a big file
    path[tail - 1] = my_cnt1;
    path[tail] = my_cnt2;

    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    int file_size = 2 * 1024 * 1024;
    raw_write(dst_fs, fd, file_size);
    raw_close(dst_fs, fd);
  }

  printf("-----------------random read big file-----------------\r\n\r\n");
  total_time_reset();
  start= (uint32_t)xTaskGetTickCount();
  temp_start= (uint32_t)xTaskGetTickCount();
  int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
  temp_end = (uint32_t)xTaskGetTickCount();
  total_open+= temp_end - temp_start;

  int test_size = 20 * 1024;
  while (test_size > 0) {
    int min = (test_size > 1024) ? 1024 : test_size;
    temp_start= (uint32_t)xTaskGetTickCount();
    raw_lseek(dst_fs, fd, rand() % (2 * 1024 * 1024));
    nfvfs_fsync(dst_fs, fd);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_lseek+= temp_end - temp_start;

    temp_start= (uint32_t)xTaskGetTickCount();
    raw_read(dst_fs, fd, min);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_read+= temp_end - temp_start;

    test_size -= min;
  }

  temp_start= (uint32_t)xTaskGetTickCount();
  raw_close(dst_fs, fd);
  temp_end = (uint32_t)xTaskGetTickCount();
  total_close+= temp_end - temp_start;

  end = (uint32_t)xTaskGetTickCount();
  total_time_print();
  printf("total random read time is %u\r\n", end - start);

  printf("-----------------random write big file-----------------\r\n\r\n");
  total_time_reset();
  start= (uint32_t)xTaskGetTickCount();

  temp_start= (uint32_t)xTaskGetTickCount();
  fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
  temp_end = (uint32_t)xTaskGetTickCount();
  total_open+= temp_end - temp_start;

  test_size = 20 * 1024;
  while (test_size > 0) {
    int min = (test_size > 1024) ? 1024 : test_size;
    temp_start= (uint32_t)xTaskGetTickCount();
    raw_lseek(dst_fs, fd, rand() % (2 * 1024 * 1024));
    nfvfs_fsync(dst_fs, fd);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_lseek+= temp_end - temp_start;

    temp_start= (uint32_t)xTaskGetTickCount();
    raw_write(dst_fs, fd, min);
    nfvfs_fsync(dst_fs, fd);
    temp_end = (uint32_t)xTaskGetTickCount();
    total_read+= temp_end - temp_start;

    test_size -= min;
  }

  temp_start= (uint32_t)xTaskGetTickCount();
  raw_close(dst_fs, fd);
  temp_end = (uint32_t)xTaskGetTickCount();
  total_close+= temp_end - temp_start;

  end = (uint32_t)xTaskGetTickCount();
  total_time_print();
  printf("total random write time is %u\r\n", end - start);

  raw_unmount(dst_fs);
  printf("-----------------operation test end-----------------\r\n\r\n");
}

// Motivation test-normal performance
void IO_stack_test(const char *fsname, int sfile_num, int rwrite_times)
{
  // Get and mount file system
  printf("-----------------IO stack test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }

  raw_mount(dst_fs);

  // init file path
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/test1.?");
  int tail = strlen(path);
  char my_cnt1 = 'A';
  char my_cnt2 = 'A';

  printf("-----------------Sequential Write Test-----------------\r\n\r\n");
  // create a big file
  path[tail - 1] = my_cnt1;
  path[tail] = my_cnt2;

  int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
  my_cnt1++;
  if (my_cnt1 == 'z') {
    my_cnt1 = 'A';
    my_cnt2 += 1;
  }
  int file_size = 2 * 1024 * 1024;

  nor_flash_message_reset();
  uint32_t start = (uint32_t)xTaskGetTickCount();
  raw_write(dst_fs, fd, file_size);
  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("Sequential write time is %u\r\n", end - start);
  nor_flash_message_print();


  printf("-----------------Random Update Test-----------------\r\n\r\n");
  nor_flash_message_reset();
  start = (uint32_t)xTaskGetTickCount();

  for (int i = 0; i < 20; i++) {
    int temp = rand() % file_size;
  }

  for (int i= 0; i < rwrite_times; i++) {
    int temp = rand() % file_size;

    raw_lseek(dst_fs, fd, temp);
    raw_write(dst_fs, fd, 1024);
    nfvfs_fsync(dst_fs, fd);
  }
  end = (uint32_t)xTaskGetTickCount();
  printf("random update time is %u\r\n", end - start);
  nor_flash_message_print();

  raw_close(dst_fs, fd);

  raw_unmount(dst_fs);
  printf("-----------------IO Stack Test end-----------------\r\n\r\n");

}

// Motivation test-raw performance
void raw_io_stack_test(int file_size, int sfile_num, int rwrite_times)
{
  // char buff[4096];
  // uint32_t start = (uint32_t)xTaskGetTickCount();
  // for (int j = 0; j < 10; j++) {
  //   for (int i = 0; i < 8192; i++) {
  //     W25QXX_Read((uint8_t *)buff, i * 4096, 16);
  //   }
  // }
  // uint32_t end = (uint32_t)xTaskGetTickCount();
  // printf("10 loop read time is %u\r\n", end - start);
  
  printf("-----------------raw IO stack test begin-----------------\r\n\r\n");

  char buff[4096];
  printf("-----------------sequential write begin-----------------\r\n");
  int sector_num = 2 * 1024 * 1024 / (4096 - 16);
  uint32_t start = (uint32_t)xTaskGetTickCount();
  for (int j = 0; j < sector_num; j++) {
    for (int i = 0; i < 8192; i++) {
      // find if exists, find the id, find the new space
      W25QXX_Read((uint8_t *)buff, i * 4096, 16);
    }
  }
  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("Sequential write additional time is %u\r\n", end - start);


  printf("-----------------Random Update begin-----------------\r\n");
  start = (uint32_t)xTaskGetTickCount();
  sector_num = 2 * 1024 * 1024 / (4096 - 16);
  int rewrite_times = 0;
  int pointer_num = 0;
  for (int i= 0; i < rwrite_times; i++) {
    int update_sector = (rand() % (2 * 1024 * 1024)) / (4096 - 16);
    pointer_num += sector_num - update_sector;
    rewrite_times += update_sector;
  }

  printf("rewrite_times, pointer_num: %d, %d", rewrite_times, pointer_num);
  // assert(rewrite_times < 7000);
  // assert(pointer_num < 7000);
  int cnt = 8190;
  for (int i = 0; i < pointer_num; i++) {
    W25QXX_Read((uint8_t *)buff, cnt * 4096, 16);
    cnt--;
  }
  cnt = 8190;
  for (int i = 0; i < rewrite_times; i++) {
    for (int j = 0; j < 8192; j++) {
      W25QXX_Read((uint8_t *)buff, j * 4096, 16);
    }
    W25QXX_Erase_Sector(cnt);
    W25QXX_Write_NoCheck((uint8_t *)buff, cnt * 4096, 1024);
    cnt--;
    if (cnt == 100)
      cnt = 8190;
  }
  end = (uint32_t)xTaskGetTickCount();
  printf("Random Update time is %u\r\n", end - start);

  printf("-----------------raw IO stack test end-----------------\r\n\r\n");
}

// IO performance test
void create_io_stack_test(const char *fsname, int sfile_num)
{
  // Get and mount file system
  printf("-----------------small file test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }

  raw_mount(dst_fs);

  // init file path
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/test1.?");
  int tail = strlen(path);
  char my_cnt1 = 'A';
  char my_cnt2 = 'A';

  // small file test
  printf("-----------------prog small file-----------------\r\n\r\n");
  nor_flash_message_reset();
  uint32_t start = (uint32_t)xTaskGetTickCount();
  for (int i = 0; i < sfile_num; i++) {
    // create new small files
    path[tail - 1] = my_cnt1;
    path[tail] = my_cnt2;

    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_close(dst_fs, fd);

    my_cnt1++;
    if (my_cnt1 == 'z') {
      my_cnt1 = 'A';
      my_cnt2 += 1;
    }
  }
  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("create time is %u\r\n", end - start);
  nor_flash_message_print();
}

// Motivation test-SPIFFS's WL overhead
void create_raw_io_stack_test(int sfile_num)
{
  printf("-----------------create_raw_io_stack_test IO stack test begin-----------------\r\n\r\n");

  char buff[4096];
  printf("-----------------create begin-----------------\r\n");
  nor_flash_message_reset();
  uint32_t start = (uint32_t)xTaskGetTickCount();
  for (int j = 0; j < sfile_num; j++) {
    for (int i = 0; i < 8192; i++) {
      // find if exists, find the id, find the new space
      W25QXX_Read((uint8_t *)buff, i * 4096, 16);
    }
    // write two data blocks
    W25QXX_Write_NoCheck((uint8_t *)buff, 1000 * 4096, 128);
    W25QXX_Write_NoCheck((uint8_t *)buff, 1000 * 4096, 16);
  }
  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("create time is %u\r\n", end - start);
  nor_flash_message_print();
}

// frequent read headers of each sector
void heads_read_test(int read_size, int loop) {

  char buff[4096];
  printf("-----------------data swap begin-----------------\r\n\r\n");
  nor_flash_message_reset();
  uint32_t start = (uint32_t)xTaskGetTickCount();
  // 1. headers
  for (int j = 0; j < 8192; j++) {
    W25QXX_Read((uint8_t *)buff, 6000 * 4096, 4);
  }
  // // 2. sort array
  // W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 1024);
  // // 3. region swap
  // int temp_sector = 2000;
  // for (int j = 0; j < 1 * 64 * 3; j++) {
  //   W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 4096);
  //   W25QXX_Read((uint8_t *)buff, 6000 * 4096, 4096);
  //   W25QXX_Erase_Sector(temp_sector++);
  //   W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 1);
  // }
  // // 4. metadata update, journal + indexes + dual bitmap
  // W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 32);
  // for (int j = 0; j < 9; j++) {
  //   W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 300);
  // }
  for (int j = 0; j < 8; j++) {
    W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 256);
  }
  W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 16);
  W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 16);
  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("time is %u\r\n", end - start);
  nor_flash_message_print();
  printf("-----------------data swap end-----------------\r\n\r\n");
  
  // char buff[4096];
  // printf("-----------------dual bitmap begin-----------------\r\n\r\n");
  // nor_flash_message_reset();
  // uint32_t start = (uint32_t)xTaskGetTickCount();
  // // for (int j = 0; j < 8192; j++) {
  // //   W25QXX_Read((uint8_t *)buff, 6000 * 4096, read_size);
  // // }
  // for (int j = 0; j < 8; j++) {
  //   W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 256);
  // }
  // W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 16);
  // W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 16);
  // uint32_t end = (uint32_t)xTaskGetTickCount();
  // printf("header read time is %u\r\n", end - start);
  // nor_flash_message_print();
  // printf("-----------------dual bitmap end-----------------\r\n\r\n");
  
  // char buff[4096];
  // printf("-----------------header read test begin-----------------\r\n\r\n");
  // nor_flash_message_reset();
  // uint32_t start = (uint32_t)xTaskGetTickCount();
  // for (int i = 0; i < loop; i++) {
  //   for (int j = 0; j < 8192; j++) {
  //     W25QXX_Read((uint8_t *)buff, i * 4096, read_size);
  //   }
  // }
  // uint32_t end = (uint32_t)xTaskGetTickCount();
  // printf("header read time is %u\r\n", end - start);
  // nor_flash_message_print();
  // printf("-----------------header read test end-----------------\r\n\r\n");
}

// WL Overhead-logging 
void device_management_logging(int log_size, int entry_num)
{
  // one log, log size 16B, entry number 1000
  // sector header 4B, name header 11B
  // available sector size 4084B

  char buff[4096];
  printf("-----------------logging test begin-----------------\r\n\r\n");
  nor_flash_message_reset();
  uint32_t start = (uint32_t)xTaskGetTickCount();

  // read & write header
  W25QXX_Read((uint8_t *)buff, 5000 * 4096, 256);
  W25QXX_Write_NoCheck((uint8_t *)buff, 5000 * 4096, 11);

  // append log
  // about 49 new sectors
  int off= 0;
  int address = 1000 * 4096;
  int alloc_num= 1;
  for (int i = 0; i <= entry_num; i++) {
    if (off == 0) {
      W25QXX_Write_NoCheck((uint8_t *)buff, address, 12);
      address+= 12;
    }
    W25QXX_Write_NoCheck((uint8_t *)buff, address, log_size);
    address+= log_size;
    off += log_size;
    if (off + log_size >= 4096) {
      W25QXX_Read((uint8_t *)buff, 5000 * 4096, 4);
      W25QXX_Write_NoCheck((uint8_t *)buff, address, 1);
      address+=1;
      off = 0;

      alloc_num+=1;
    }
  }

  // for (int i = 0; i < alloc_num; i++) {
  //   for (int j = 0; j < 8192; j++) {
  //     W25QXX_Read((uint8_t *)buff, j * 4096, 4);
  //   }
  // }

  int loop_num= 0;
  int begin_sector = 5000;
  while (alloc_num > 0) {
    W25QXX_Read((uint8_t *)buff, begin_sector * 4096, 4);
    if ((rand() % 80) == 1) {
      alloc_num -= 1;
    }
    loop_num+=1;
  }
  printf("loop %d sectors\r\n", loop_num);

  // write index
  W25QXX_Write_NoCheck((uint8_t *)buff, address, 16);
  address += 16;

  delay_ms(400);

  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("total logging time is %u, %u, %u\r\n", end - start, start, end);
  nor_flash_message_print();
  printf("-----------------logging test end-----------------\r\n\r\n");
}

// WL Overhead-OTA
void device_management_ota(void)
{
  char buff[4096];
  printf("-----------------OTA test begin-----------------\r\n\r\n");
  nor_flash_message_reset();
  uint32_t start = (uint32_t)xTaskGetTickCount();

  // for (int i = 0; i < 259; i++) {
  //   for (int j = 0; j < 8192; j++) {
  //     W25QXX_Read((uint8_t *)buff, j * 4096, 4);
  //   }
  // }

  int loop_num= 0;
  int begin_sector = 5000;
  int alloc_num = 259;
  while (alloc_num > 0) {
    W25QXX_Read((uint8_t *)buff, begin_sector * 4096, 4);
    if ((rand() % 80) == 1) {
      alloc_num -= 1;
    }
    loop_num+=1;
  }
  printf("loop %d sectors\r\n", loop_num);

  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("additional ota time is %u, %u, %u\r\n", end - start, start, end);
  nor_flash_message_print();
  printf("-----------------OTA test end-----------------\r\n\r\n");
}

// IO performance test-Ideal
void ideal_io_test(void)
{
  printf("-----------------Ideal IO test begin-----------------\r\n\r\n");

  printf("-----------------aging sector allocation begin-----------------\r\n");
  char buff[4096];
  int need_sectors[2], loop_num[2];
  need_sectors[0] = (2 * 1024 * 1024) / (4096 - 12) + 1;
  need_sectors[1] = 20;

  for (int i = 0; i < 2; i++) {
    loop_num[i] = 0;

    int alloc_num = need_sectors[i];
    while (alloc_num > 0) {
      W25QXX_Read((uint8_t *)buff, 5000 * 4096, 4);
      if ((rand() % 80) == 1) {
        alloc_num -= 1;
      }
      loop_num[i] += 1;
    }
  }

  printf("sectors number (sequential, random): %d, %d\r\n", loop_num[0], loop_num[1]);

  for (int i = 0; i < 2; i++) {
    uint32_t start = (uint32_t)xTaskGetTickCount();
    int my_cnt = loop_num[i];
    while (my_cnt > 0) {
      W25QXX_Read((uint8_t *)buff, 5000 * 4096, 4);
      W25QXX_Write_NoCheck((uint8_t *)buff, 5000 * 4096, 4);
      my_cnt--;
    }
    uint32_t end = (uint32_t)xTaskGetTickCount();
    printf("greedy WL additional time %d\r\n", end - start);
  }

  for (int i = 0; i < 2; i++) {
    uint32_t start = (uint32_t)xTaskGetTickCount();
    int my_cnt = (loop_num[i] / 64) + 1;
    while (my_cnt > 0) {
      W25QXX_Read((uint8_t *)buff, 5000 * 4096, 8);
      W25QXX_Write_NoCheck((uint8_t *)buff, 5000 * 4096, 8);
      my_cnt--;
    }
    uint32_t end = (uint32_t)xTaskGetTickCount();
    printf("region WL additional time %d\r\n", end - start);
  }
      
  printf("-----------------aging sector allocation end-----------------\r\n");

  // printf("-----------------Erase 10 sectors begin-----------------\r\n");
  // int erase_pos = 500;
  // uint32_t start = (uint32_t)xTaskGetTickCount();
  // for (int j = 0; j < 10; j++) {
  //   W25QXX_Erase_Sector(erase_pos++);
  // }
  // uint32_t end = (uint32_t)xTaskGetTickCount();
  // printf("Erase 10 sectors time is %u\r\n", end - start);

  // char buff[4096];
  // printf("-----------------2 MB sequential write begin-----------------\r\n");
  // int sector_num = 2 * 1024 * 1024 / 4096;
  // uint32_t start = (uint32_t)xTaskGetTickCount();
  // for (int j = 0; j < sector_num; j++) {
  //   W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, 4096);
  // }
  // uint32_t end = (uint32_t)xTaskGetTickCount();
  // printf("Sequential write time is %u\r\n", end - start);


  // printf("-----------------20 KB Random write begin-----------------\r\n");
  // sector_num = 5;
  // start = (uint32_t)xTaskGetTickCount();
  // for (int j = 0; j < sector_num; j++) {
  //   W25QXX_Write_NoCheck((uint8_t *)buff, (6000 + j) * 4096, 4096);
  // }
  // end = (uint32_t)xTaskGetTickCount();
  // printf("Random write time is %u\r\n", end - start);

  // printf("-----------------20 KB read begin-----------------\r\n");
  // sector_num = 5;
  // start = (uint32_t)xTaskGetTickCount();
  // for (int j = 0; j < sector_num; j++) {
  //   W25QXX_Read((uint8_t *)buff, (6000 + j) * 4096, 4096);
  // }
  // end = (uint32_t)xTaskGetTickCount();
  // printf("read time is %u\r\n", end - start);

  // printf("-----------------20 KB write begin-----------------\r\n");
  // sector_num = 5;
  // start = (uint32_t)xTaskGetTickCount();
  // for (int j = 0; j < sector_num; j++) {
  //   W25QXX_Write_NoCheck((uint8_t *)buff, (6000 + j) * 4096, 4096);
  // }
  // end = (uint32_t)xTaskGetTickCount();
  // printf("write time is %u\r\n", end - start);

  // printf("-----------------small read begin-----------------\r\n");
  // sector_num = 10;
  // int read_size = 2;
  // for (int j = 0; j < sector_num; j++) {
  //   start = (uint32_t)xTaskGetTickCount();
  //   for (int k = 0; k < 1000; k++) {
  //     W25QXX_Read((uint8_t *)buff, 6000 * 4096, read_size);
  //   }
  //   end = (uint32_t)xTaskGetTickCount();
  //   printf("small read time is %d, %u\r\n", read_size, end - start);
  //   read_size *= 2;
  // }

  // printf("-----------------small write begin-----------------\r\n");
  // sector_num = 10;
  // int write_size = 2;
  // for (int j = 0; j < sector_num; j++) {
  //   start = (uint32_t)xTaskGetTickCount();
  //   for (int k = 0; k < 1000; k++) {
  //     W25QXX_Write_NoCheck((uint8_t *)buff, 6000 * 4096, write_size);
  //   }
  //   end = (uint32_t)xTaskGetTickCount();
  //   printf("small write time is %d, %u\r\n", write_size, end - start);
  //   write_size *= 2;
  // }
  
  printf("-----------------Ideal IO test end-----------------\r\n\r\n");
}

// Total time used by dual bitmap and allocation
void dual_bitmap_io_time(int file_size, int rwrite_times)
{
  printf("-----------------dual_bitmap_io_time test begin-----------------\r\n\r\n");

  char buff[4096];
  printf("-----------------sequential write begin-----------------\r\n");
  int io_num = (2 * 1024 * 1024 / (4096 - 16)) / 64 + 1;
  uint32_t start = (uint32_t)xTaskGetTickCount();
  for (int j = 0; j < io_num; j++) {
    W25QXX_Read((uint8_t *)buff, 4096 * 4096, 8);
    W25QXX_Write_NoCheck((uint8_t *)buff, 4096 * 4096, 8);
  }
  uint32_t end = (uint32_t)xTaskGetTickCount();
  printf("Sequential write additional time is %u\r\n", end - start);


  printf("-----------------Random Update begin-----------------\r\n");
  start = (uint32_t)xTaskGetTickCount();
  io_num = rwrite_times / 64 + 1;
  for (int j = 0; j < io_num; j++) {
    W25QXX_Read((uint8_t *)buff, 4096 * 4096, 8);
    W25QXX_Write_NoCheck((uint8_t *)buff, 4096 * 4096, 8);
  }
  end = (uint32_t)xTaskGetTickCount();
  printf("Random Update additional time is %u\r\n", end - start);
  
  printf("-----------------raw IO stack test end-----------------\r\n\r\n");
	
}
