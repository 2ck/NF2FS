#include "nfvfs.h"
#include "nor_flash_simulate.h"
#include "NORbench.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "NF2FS_rw.h"
// #include <sys/types.h>
#include "NF2FS_manage.h"

uint32_t random_data[60] = {499888, 8651, 1342281, 55400, 437511, 
                            152389, 1776584,  2051967,  1667859,  569284, 
                            462956, 1263403,  564154,   966241,   514405,
                            1189793,568434,   1597540,  226105,   1435197,
                            788974, 1042130,  518837,   1683802,  1466937,
                            1828712,1048040,  946377,   764586,   307036,
                            691871, 645680,   379073,   1293912,  568026,
                            438873, 520895,   912605,   915164,   1878811,
                            1977457,2002619,  1644067,  1745957,  825244,
                            911733, 1535869,  1491854,  879010,   646863,
                            1017315,1591742,  2045619,  1850849,  691872,
                            1145568,387724,   539041,   623426,   876384};

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
  // NEXT
  // nfvfs_fsync(fs, fd);
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

  for (int i = 0; i < loop; i++) {
    raw_lseek(fs, fd, 0);
    raw_write(fs, fd, len);
  }

  for (int i = 0; i < loop; i++) {
    raw_lseek(fs, fd, 0);
    raw_read(fs, fd, len);
  }

  raw_close(fs, fd);
}

// sequential read/write slen at pos 0, random read/write rlen at pos off
void raw_sr_test(struct nfvfs *fs, char *path, int slen, int off, int rlen)
{
  int fd;
  printf("-----------------raw_sr_test-----------------\r\n");
  fd = raw_open(fs, path, O_RDWR | O_CREAT, S_ISREG);

  // sequential write
  raw_lseek(fs, fd, 0);
  raw_write(fs, fd, slen);

  // sequential read
  raw_lseek(fs, fd, 0);
  raw_read(fs, fd, slen);

  // random write
  raw_lseek(fs, fd, slen - 1);
  raw_lseek(fs, fd, off);
  raw_write(fs, fd, rlen);

  // random read
  raw_lseek(fs, fd, slen - 1);
  raw_lseek(fs, fd, off);
  raw_read(fs, fd, rlen);

  raw_close(fs, fd);
}

// test the sequential read/write performance of NF2FS
// NEXT, may change
void raw_total_sr_test(struct nfvfs *fs, char *path, int tail, char *my_cnt, int off)
{
  printf("\r\n\r\n\r\n");
  printf("-----------------raw_total_sr_test-----------------\r\n");

  path[tail] = *my_cnt;
  raw_sr_test(fs, path, 2 * 1024 * 1024, off, 1024);
  *my_cnt += 1;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------------    Basic fs test    -------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// test basic operations for varing sizes files
void file_operations_test(const char *fsname, int loop)
{
  struct nfvfs *fs;

  // get and mount file system
  printf("-----------------begin-----------------\r\n");
  fs = get_nfvfs(fsname);
  if (!fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }
  raw_mount(fs);

  // init file
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/test1.txt");
  int tail = strlen(path) - 1;
  char my_cnt = 'A';

  // test basic operations for small files   
  int size = 8;
  for (int i = 0; i < 5; i++) {
    printf("\r\n\r\n-----------------%dB file test-----------------\r\n", size);
    path[tail] = my_cnt++;
    raw_basic_test(fs, path, size, loop);
    size = size * 4;
  }

  // test basic operations for big files
  size = 8 * 1024;
  for (int i = 0; i < 5; i++) {
    printf("\r\n\r\n-----------------%dB file test-----------------\r\n", size);
    path[tail] = my_cnt++;
    raw_basic_test(fs, path, size, 1);
    size = size * 4;
  }

  // umount file
  printf("\r\n\r\n\r\n");
  raw_unmount(fs);
  printf("-----------------end-----------------\r\n");
}

void layer_basic_test(struct nfvfs *dst_fs, char *path, int len, int loop)
{
  char a = 'a';
  char b = 'a';
  int fd;

  // test for files varing 2B to 1024B
  int size = 2;
  for (int i = 0; i < 10; i++) {
    path[len] = a;
    b = 'a';

    printf("\r\n-----------------create %dB file-----------------\r\n",size);

    // write files
    fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    for (int j = 0; j < loop; j++) {
      printf("\r\n-----------------loop %d write-----------------\r\n", j);
      raw_lseek(dst_fs, fd, 0);
      raw_write(dst_fs, fd, size);
    }
    raw_close(dst_fs, fd);

    // read files
    printf("\r\n-----------------open %dB file again-----------------\r\n",size);
    fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    for (int j = 0; j < loop; j++) {
      printf("\r\n-----------------loop %d read-----------------\r\n", j);
      raw_lseek(dst_fs, fd, 0);
      raw_read(dst_fs, fd, size);
    }

    printf("\r\n-----------------close %dB file-----------------\r\n",size);
    raw_close(dst_fs, fd);
    size = size * 2;
    a++;
  }
}

void layer_test(struct nfvfs *dst_fs, char *path, int loop)
{
  // test module
  int len = strlen(path);
  layer_basic_test(dst_fs, path, len, loop);

  // ready for the next loop
  strcpy(&path[len], "layer");
  int fd = raw_open(dst_fs, path, 0x0100, S_ISDIR);
  len = strlen(path);
  path[len] = '/';
  //   raw_close(dst_fs, fd);
}

// test file operations with dir layers
void dir_operations_test(const char *fsname, int dir_layer, int loop)
{
  struct nfvfs *dst_fs;

  printf("\r\n\r\n\r\n-----------------dir test begin-----------------\r\n");

  // get and mount file system
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }
  raw_mount(dst_fs);

  // init for dir layer
  char buff[128];
  memset(buff, 0, 128);
  strcpy(buff, "/");

  // test for varing sizes dir layers
  for (int i = 0; i < dir_layer; i++) {
    printf("\r\n-----------------dir layer %d-----------------\r\n", i);
    layer_test(dst_fs, buff, loop);
  }

  printf("\r\n\r\n\r\n-----------------unmount fs-----------------\r\n");
  raw_unmount(dst_fs);

  printf("\r\n\r\n\r\n-----------------dir test end-----------------\r\n");
}

// test mount operations
void mount_test(const char *fsname)
{
  W25QXX_init();

  printf("\r\n\r\n\r\n-----------------mount test begin-----------------\r\n");

  // get file system
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }

  // mount file system
  for (int i = 0; i < 100; i++) {
    printf("loop %d\r\n\r\n", i);
    printf("\r\n\r\n\r\n-----------------mount-----------------\r\n");
    raw_mount(dst_fs);
    printf("\r\n\r\n\r\n-----------------unmount-----------------\r\n");
    raw_unmount(dst_fs);
  }
  printf("\r\n\r\n\r\n-----------------mount test end-----------------\r\n");
}

// test random write of the big file
void random_write_test(const char *fsname, int rwrite_times)
{
  // get and mount file system
  printf("-----------------random write test begin-----------------\r\n\r\n");
  struct nfvfs *dst_fs;
  dst_fs = get_nfvfs(fsname);
  if (!dst_fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }
  raw_mount(dst_fs);

  // create dir path of /busybox/bin
  printf("-----------------create dir path-----------------\r\n\r\n");
  char path[64];
  memset(path, 0, 64);
  strcpy(path, "/busybox");
  int fd = raw_open(dst_fs, path, 0x0100, S_ISDIR);
  strcpy(path, "/busybox/bin");
  fd = raw_open(dst_fs, path, 0x0100, S_ISDIR);

  // create and write file /busybox/bin/busybox
  printf("-----------------init big file-----------------\r\n\r\n");
  strcpy(path, "/busybox/bin/busybox");
  fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
  if (fd < 0)
    printf("open error is %d\n", fd);
  raw_write(dst_fs, fd, 2 * 1024 * 1024);
  raw_close(dst_fs, fd);

  // random write test
  printf("-----------------test random write-----------------\r\n\r\n");
  fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
  int test_size = rwrite_times * 1024;
  while (test_size > 0) {
    printf("-----------------rest %dB data-----------------\r\n\r\n", test_size);
    int min = (test_size > 1024) ? 1024 : test_size;
    raw_lseek(dst_fs, fd, rand() % (2 * 1024 * 1024));
    raw_write(dst_fs, fd, min);
    test_size -= min;
  }
  raw_close(dst_fs, fd);
  
  raw_unmount(dst_fs);
  printf("-----------------random write test end-----------------\r\n\r\n");
}

int global_cnt= 0;
void write_busybox_test(char *path, struct nfvfs *dst_fs, const char *fsname)
{
  DIR *my_dir = NULL;
  struct dirent *dir_entry;
  unsigned int i;
  int fno;

  // the begin address of busybox
  char dst_path[256];
  memset(dst_path, 0, 256);
  strcpy(dst_path, &path[11]);

  // create the dir path
  // printf("-----------------dir path is %s-----------------\r\n", dst_path);
  int dst_i = strlen(dst_path);
  int fd = raw_open(dst_fs, dst_path, 0x0100, S_ISDIR);

  // loop to write
  my_dir = opendir(path);
  if (my_dir != NULL) {
    while (true) {
      dir_entry = readdir(my_dir);

      if (dir_entry == NULL)
        break;
      else if (dir_entry->d_name[0] == '.')
        continue;

      if (dir_entry->d_type == 4) {
        // It's dir
        i = strlen(path);
        path[i]= '/';
        memcpy(&path[i+1], dir_entry->d_name, strlen(dir_entry->d_name));

        global_cnt++;
        write_busybox_test(path, dst_fs, fsname);
        memset(&path[i], '\0', 255 - i);
      } else {
        // It's file

        // create NF2FS file
        dst_path[dst_i]= '/';
        memcpy(&dst_path[dst_i+1], dir_entry->d_name, strlen(dir_entry->d_name));
        int fd2 = raw_open(dst_fs, dst_path, O_RDWR | O_CREAT, S_ISREG);
          
        // open origin file and get file len
        i = strlen(path);
        path[i]= '/';
        memcpy(&path[i+1], dir_entry->d_name, strlen(dir_entry->d_name));
        FILE *my_file= fopen(path, "r");
        fseek(my_file, 0, SEEK_END);

        // write data to NF2FS file and change states
        raw_write(dst_fs, fd2, ftell(my_file));
        fclose(my_file);
        memset(&path[i], '\0', 255 - i);

        raw_close(dst_fs, fd2);
        global_cnt++;
      }
    }
    closedir(my_dir);
  }
  raw_close(dst_fs, fd);
}

int read_busybox_test(char *path, struct nfvfs *dst_fs)
{
  // open the big file in busybox
  int fd = raw_open(dst_fs, path, O_APPEND, S_ISREG);
  if (fd < 0) {
    printf("open reg file error: %s, %d\r\n", path, fd);
    return fd;
  }

  // read data and close file
  raw_lseek(dst_fs, fd, 0);
  int my_size = 2675216;
  while (my_size > 0) {
    int size = (my_size > 4096) ? 4096 : my_size;
    raw_read(dst_fs, fd, size);
    my_size -= size;
  }
  raw_close(dst_fs, fd);

  return 0;
}

int traverse_busybox_test(char *path, struct nfvfs *dst_fs) 
{
  int err= 0;
  int cnt= 0;

  int fd = raw_open(dst_fs, path, O_APPEND, S_ISDIR);
  struct nfvfs_dentry *info = malloc(sizeof(struct nfvfs_dentry) + 256);
  while (true) {
    raw_readdir(dst_fs, fd, info);
    if (info->type == NFVFS_TYPE_END) {
      // the end
      break;
    } else if (info->type == NFVFS_TYPE_REG) {
      // it is a file
      global_cnt++;
      cnt++;
    } else if (info->type == NFVFS_TYPE_DIR) {
      // it is a dir
      char temp[256];
      strcpy(temp, path);
      int len = strlen(temp);
      temp[len] = '/';
      strcpy(&temp[len + 1], info->name);
      err = traverse_busybox_test(temp, dst_fs);
      if (err) {
        printf("....\n");
        return err;
      }
      global_cnt++;
      cnt++;
    }
  }
  return err;
}

/**
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 * -------------------------------------------------------------    new fs test    -------------------------------------------------------------
 * -----------------------------------------------------------------------------------------------------------------------------------------------
 */

// test basic operations for varing sizes files
void fs_io_test(const char *fsname)
{
  W25QXX_init();

  struct nfvfs *fs;

  // get and mount file system
  printf("-----------------begin-----------------\r\n");
  fs = get_nfvfs(fsname);
  if (!fs) {
    printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
    return;
  }
  raw_mount(fs);

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

  int random_index= 0;

  // test big file random io 
  size = 2 * 1024 * 1024;
  path[tail] = my_cnt++;
  int fd= raw_open(fs, path, O_RDWR | O_CREAT, S_ISREG);
  raw_write(fs, fd, size);
  printf("-----------------big file random read test-----------------\r\n\r\n");
  int test_size = 20 * 1024;
  while (test_size > 0) {
    int min = (test_size > 1024) ? 1024 : test_size;
    raw_lseek(fs, fd, random_data[random_index++]);
    raw_read(fs, fd, min);
    test_size -= min;
  }

  // test big file random io 
  printf("-----------------big file random write test-----------------\r\n\r\n");
  test_size = 20 * 1024;
  while (test_size > 0) {
    int min = (test_size > 1024) ? 1024 : test_size;
    raw_lseek(fs, fd, random_data[random_index++]);
    raw_write(fs, fd, min);
    test_size -= min;
  }
  raw_close(fs, fd);

  // umount file
  printf("\r\n\r\n\r\n");
  raw_unmount(fs);
  printf("-----------------end-----------------\r\n");
}

// test busybox operations, including burn, read
void Busybox_test(const char *fsname)
{
  W25QXX_init();

    // get and mount file system 
    printf("-----------------Busybox test begin-----------------\r\n\r\n");
    struct nfvfs *dst_fs;
    dst_fs = get_nfvfs(fsname);
    if (!dst_fs) {
        printf("\r\nFailed to get %s, making sure you have register it\r\n", fsname);
        return;
    }
    raw_mount(dst_fs);

    // write busybox
    printf("\r\n\r\n-----------------Write Busybox-----------------\r\n\r\n");
    char buff[256];
    memset(buff, 0, 256);
    strcpy(buff, "./test_data/busybox");
    write_busybox_test(buff, dst_fs, fsname);
    printf("total files/dirs in busybox are %d\r\n", global_cnt);

    // read busybox
    printf("\r\n\r\n-----------------Read Busybox-----------------\r\n\r\n");
    strcpy(buff, "/busybox/bin/busybox");
    read_busybox_test(buff, dst_fs);

    // traverse busybox
    global_cnt= 0;
    printf("\r\n\r\n-----------------Traverse Busybox-----------------\r\n\r\n");
    strcpy(buff, "/busybox");
    traverse_busybox_test(buff, dst_fs);
    printf("total files/dirs in busybox are %d\r\n", global_cnt);

    raw_unmount(dst_fs);
    printf("-----------------Busybox test end-----------------\r\n\r\n");
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
  path[tail - 1] = 'A';
  char temp_cnt= '1';
  for (int j = 0; j < 40000; j++) {
    printf("loop %d\n", j);
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_lseek(dst_fs, fd, 0);
    for (int i = 0; i < 16 * 1024; i++) {
      raw_write(dst_fs, fd, 32);
    }
    if (fsname[0] == 'l' && fsname[1] == 'i')
      raw_close(dst_fs, fd);
    raw_delete(dst_fs, fd, path, S_ISREG);

    if (j % 10000 == 9999) {
      char temp_name[64];
      memset(temp_name, 0, 64);
      strcpy(temp_name, "./out/");
      int temp_len = strlen(temp_name);
      strcpy(&temp_name[temp_len], fsname);
      temp_len = strlen(temp_name);
      temp_name[temp_len] = '_';
      temp_name[temp_len + 1] = temp_cnt;
      temp_cnt++;
      Erase_Times_Print(temp_name);
    }
  }

  // Unmount fs
  raw_unmount(dst_fs);
  printf("-----------------wl test end-----------------\r\n\r\n");
}

// test the gc performance of NF2FS
void gc_test(const char *fsname, int sfile_num, int rwrite_times)
{
  W25QXX_init();

  // Get and mount file system
  printf("-----------------gc test begin-----------------\r\n\r\n");
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

  // small file gc
  in_place_size_reset();
  printf("-----------------dir gc-----------------\r\n\r\n");
  for (int i = 0; i < sfile_num; i++) {
    // create new small files
    path[tail - 1] = my_cnt1;
    path[tail] = my_cnt2;
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_write(dst_fs, fd, 32);
    my_cnt1++;
    if (my_cnt1 == 'z') {
      my_cnt1 = 'A';
      my_cnt2 += 1;
    }

    if (i % 2 == 0) {
      raw_close(dst_fs, fd);
    } else {
      nfvfs_fsync(dst_fs, fd);
      raw_delete(dst_fs, fd, path, S_ISREG);
    }

    if (i % 500 == 499) {
      printf("a new record point\r\n");
    }
  }
  in_place_size_print();

  // // NEXT
  // assert(-1 > 0);

  // big file gc begin
  in_place_size_reset();
  printf("-----------------big file gc-----------------\r\n\r\n");
  for (int i = 0; i < 1; i++) {
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
    raw_write(dst_fs, fd, file_size);

    // random write and gc
    int test_size = rwrite_times * 1024;
    for (int i= 0; i < rwrite_times; i++) {
      int temp = rand() % file_size;
      raw_lseek(dst_fs, fd, temp);
      raw_write(dst_fs, fd, 1024);
    }
    raw_close(dst_fs, fd);
  }
  in_place_size_print();

  raw_unmount(dst_fs);
  printf("-----------------gc test end-----------------\r\n\r\n");
}

// case study of data logging
void logging_test(const char *fsname, int log_size, int loop, int entry_num)
{
  W25QXX_init();

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

    printf("-----------------logging size %d-----------------\r\n\r\n", log_size);
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    for (int j= 0; j < entry_num; j++) {
      raw_write(dst_fs, fd, log_size);
    }
    raw_close(dst_fs, fd);
    log_size= log_size * 2;
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
    // init for current sector + crc
    super_off= 0;
    log_off= 8192;
    W25QXX_Write_NoCheck(buff, super_off, 8);
    super_off+= 8;
    
    // write log entry
    for (int j= 0; j < entry_num; j++) {
      // current sector can not store new log
      if ((4096 - (log_off % 4096)) < (log_size + 4)) {
        log_off+= 4096 - log_off % 4096;
        W25QXX_Write_NoCheck(buff, super_off, 8);
        super_off+= 8;
        if (super_off >= 4096) {
          W25QXX_Erase_Sector(0);
          super_off= 0;
        }
      }

      // write new log
      W25QXX_Write_NoCheck(buff, log_off, log_size + 4);
      log_off+= log_size + 4;
      assert(log_off % 4096 != 0);
    }
  }

  printf("-----------------raw logging test end-----------------\r\n\r\n");
}

// case study of ota update
void ota_test(const char *fsname, int file_size, int loop, int ota_times)
{
  W25QXX_init();

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

  // test logging
  for (int i= 0; i < loop; i++) {
    path[tail - 1] = my_cnt1;
    my_cnt1++;

    printf("-----------------file_size %d-----------------\r\n\r\n", file_size);
    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_write(dst_fs, fd, file_size);
    alloc_num_reset();
    alloc_num_print();
    for (int j= 0; j < ota_times; j++) {
      raw_lseek(dst_fs, fd, rand() % file_size);
      raw_write(dst_fs, fd, 1024);
    }
    alloc_num_print();
    raw_close(dst_fs, fd);
    file_size= file_size * 2;
  }

  raw_unmount(dst_fs);
  printf("-----------------ota test end-----------------\r\n\r\n");
}

// case study of ota update
void raw_ota_test(int file_size, int loop)
{
  printf("-----------------raw ota test begin-----------------\r\n\r\n");

  char buff[4096];
  for (int i= 0; i < loop; i++) {
    printf("file size %d\r\n", file_size);
    int rest_size= file_size;
    int address= 4 * 4096;
    while (rest_size != 0) {
      W25QXX_Write_NoCheck(buff, address, 4096);
      W25QXX_Erase_Sector(address / 4096);
      address+= 4096;
      rest_size-= 4096;
    }
    file_size= file_size * 2;
  }
  
  printf("-----------------raw ota test end-----------------\r\n\r\n");
}

// Test the inefficiency of multi-layer IO stack and device management
void IO_stack_test(const char *fsname, int sfile_num, int rwrite_times)
{
  W25QXX_init();

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

  // small file gc
  printf("-----------------Create Test-----------------\r\n\r\n");
  // nor_flash_message_reset();
  // uint32_t start = (uint32_t)xTaskGetTickCount();
  for (int i = 0; i < sfile_num; i++) {
    // create new small files
    path[tail - 1] = my_cnt1;
    path[tail] = my_cnt2;

    printf("%d, %s\r\n", i, path);

    int fd = raw_open(dst_fs, path, O_RDWR | O_CREAT, S_ISREG);
    raw_close(dst_fs, fd);
    my_cnt1++;
    if (my_cnt1 == 'z') {
      my_cnt1 = 'A';
      my_cnt2 += 1;
    }
  }
  // uint32_t end = (uint32_t)xTaskGetTickCount();
  // printf("create time is %u\r\n", end - start);
  // nor_flash_message_print();

  printf("-----------------Random Update Test-----------------\r\n\r\n");
  for (int i = 0; i < 1; i++) {
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
    raw_write(dst_fs, fd, file_size);

    printf("1\r\n");

    // random write and gc
    for (int i= 0; i < rwrite_times; i++) {
      printf("loop %d\r\n", i);

      int temp = rand() % file_size;
      raw_lseek(dst_fs, fd, temp);
      raw_write(dst_fs, fd, 1024);
      nfvfs_fsync(dst_fs, fd);
    };

    raw_close(dst_fs, fd);
  }

  raw_unmount(dst_fs);
  printf("-----------------IO Stack Test end-----------------\r\n\r\n");
}

// test the performance of IO stack with multi-layer and device management
void raw_io_stack_test(int file_size, int sfile_num, int rwrite_times)
{
  printf("-----------------raw IO stack test begin-----------------\r\n\r\n");

  char buff[4096];
  printf("-----------------create begin-----------------\r\n");
  for (int j = 0; j < sfile_num; j++) {
    for (int i = 0; i < 8192; i++) {
      // find if exists, find the id, find the new space
      W25QXX_Read((uint8_t *)buff, i * 4096, 16);
    }
    // write two data blocks
    W25QXX_Write_NoCheck((uint8_t *)buff, 1000 * 4096, 128);
    W25QXX_Write_NoCheck((uint8_t *)buff, 1000 * 4096, 16);
    for (int k = 0; k <= j; k++) {
      W25QXX_Read((uint8_t *)buff, 1000 * 4096, 16);
    }
  }

  printf("-----------------Random Update begin-----------------\r\n");
  int sector_num = 2 * 1024 * 1024 / (4096 - 16);
  int rewrite_times = 0;
  int pointer_num = 0;
  for (int i= 0; i < rwrite_times; i++) {
    int update_sector = (rand() % (2 * 1024 * 1024)) / (4096 - 16);
    pointer_num += sector_num - update_sector;
    rewrite_times += update_sector;
  }

  assert(rewrite_times < 7000);
  assert(pointer_num < 7000);
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
    W25QXX_Write_NoCheck((uint8_t *)buff, cnt * 4096, 256);
    cnt--;
  }
  
  printf("-----------------raw IO stack test end-----------------\r\n\r\n");
}

// we should simulate for each configurations
// test the effectiveness of allocating and wear leveling strategies.
void device_management_logging(int log_size, int entry_num)
{
  // one log, log size 16B, entry number 1000
  // sector header 4B, name header 11B
  // available sector size 4084B

  char buff[4096];
  printf("-----------------logging test begin-----------------\r\n\r\n");

  // read & write header
  W25QXX_Read((uint8_t *)buff, 5000 * 4096, 256);
  W25QXX_Write_NoCheck((uint8_t *)buff, 5000 * 4096, 11);
  
  // append log
  int off= 0;
  int address= 1000 * 4096;
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
      off= 0;

      alloc_num+=1;
    }
  }

  int my_cnt = 0;
  while (alloc_num > 0) {
    W25QXX_Read((uint8_t *)buff, 5000 * 4096, 4);
    if ((rand() % 100) == 1) {
      alloc_num -= 1;
    }
    my_cnt++;
  }

  printf("%d\r\n", my_cnt);

  // write index
  W25QXX_Write_NoCheck((uint8_t *)buff, address, 16);
  address+=16;

  printf("-----------------logging test end-----------------\r\n\r\n");
}