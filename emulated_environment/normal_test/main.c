#include "nfvfs.h"
#include "nor_flash_simulate.h"
#include "NORbench.h"
#include "NF2FS_brigde.h"
#include <stdio.h>

extern char *sflash;

void fs_task()
{
	W25QXX_init();
	Erase_Times_Reset();
	// 1. Test basic mount/unmount operations
	mount_test("NF2FS");

	// 2. Test sequential/random I/O on varying I/O sizes
	fs_io_test("NF2FS");

	// 3. Test the GC performance
	gc_test("NF2FS", 2000, 40);

	// 4. Test Directory operations
	dir_operations_test("NF2FS", 5, 10);

	// 5. Test real-world logging
	logging_test("NF2FS", 16, 3, 1000);

	// 6. Test real-world ota updates
	ota_test("NF2FS", 1024 * 1024, 3, 30);

	// 7. Overhead breakdown of the multi-layer I/O stack
	IO_stack_test("NF2FS", 500, 20);
}

extern struct nfvfs_operations lfs_ops;
extern struct nfvfs_operations spiffs_ops;
extern struct nfvfs_operations NF2FS_ops;

void fs_registration(void)
{
	register_nfvfs("NF2FS", &NF2FS_ops, NULL);
}

void fs_unregistration(void)
{
	unregister_nfvfs("NF2FS");
}

int main(void)
{
	fs_registration();
	fs_task();
	fs_unregistration();
}
