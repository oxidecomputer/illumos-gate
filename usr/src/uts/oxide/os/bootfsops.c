/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2023 Oxide Computer Company
 */

/*
 * A bootfs module that retrieves files from the SP via IPCC.
 * This is used to retrieve /etc/system and /kernel/drv/dtrace.conf to enable
 * the use of anonymous dtrace on Oxide hardware.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/bootvfs.h>
#include <sys/filep.h>
#include <sys/reboot.h>
#include <sys/sunddi.h>
#include <sys/ccompile.h>
#include <sys/queue.h>
#include <sys/kernel_ipcc.h>
#include <sys/kobj_impl.h>
#include <sys/ipcc.h>
#include <sys/zmod.h>

struct ipcc_file {
	uint8_t if_key;
	void *if_data;

	int if_fd;
	off_t if_off;
	size_t if_size;

	SLIST_ENTRY(ipcc_file) if_next;
};

static SLIST_HEAD(ipcc_file_list, ipcc_file)
    open_files = SLIST_HEAD_INITIALIZER(open_files);

extern void *bkmem_alloc(size_t);
extern void bkmem_free(void *, size_t);

static void __PRINTFLIKE(1)
bipcc_printf(char *fmt, ...)
{
	if ((boothowto & RB_VERBOSE) != 0) {
		va_list ap;

		va_start(ap, fmt);
		_vkobj_printf(ops, fmt, ap);
		va_end(ap);
	}
}

static void
add_open_file(struct ipcc_file *file)
{
	SLIST_INSERT_HEAD(&open_files, file, if_next);
}

static void
remove_open_file(struct ipcc_file *file)
{
	SLIST_REMOVE(&open_files, file, ipcc_file, if_next);
}

static struct ipcc_file *
find_open_file(int fd)
{
	struct ipcc_file *file;

	if (fd < 0)
		return (NULL);

	SLIST_FOREACH(file, &open_files, if_next) {
		if (file->if_fd == fd)
			return (file);
	}

	return (NULL);
}

static int
bipcc_mountroot(char *str __unused)
{
	return (-1);
}

static int
bipcc_unmountroot(void)
{
	return (-1);
}

static struct {
	const char *filename;
	uint8_t key;
} file_lookup[] = {
	{ "/etc/system",		IPCC_KEY_ETC_SYSTEM },
	{ "/kernel/drv/dtrace.conf",	IPCC_KEY_DTRACE_CONF },
};

static int
bipcc_open(char *path, int flags __unused)
{
	static int filedes = 1;
	uint8_t key, *buf;
	struct ipcc_file *file;
	size_t dstlen, len, bufl;
	uint8_t *dst;
	int ret;

	key = UINT8_MAX;
	for (uint_t i = 0; i < ARRAY_SIZE(file_lookup); i++) {
		if (strcmp(path, file_lookup[i].filename) == 0)
			key = file_lookup[i].key;
	}

	if (key == UINT8_MAX)
		return (-1);

	buf = bkmem_alloc(IPCC_MAX_DATA_SIZE);

	bufl = IPCC_MAX_DATA_SIZE;
	ret = kernel_ipcc_keylookup(key, buf, &bufl);

	if (ret != 0 || bufl == 0) {
		bipcc_printf("ipcc: failed to open '%s': %d\n", path, ret);
		bkmem_free(buf, IPCC_MAX_DATA_SIZE);
		return (-1);
	}

	bipcc_printf("ipcc: opened '%s', 0x%lx bytes\n", path, bufl);

	/*
	 * Compressed objects are stored in the SP with a leading uint16_t that
	 * specifies the length of original data.
	 */
	dstlen = *(uint16_t *)buf;
	dst = bkmem_alloc(dstlen);
	len = dstlen;
	ret = z_uncompress(dst, &len, buf + sizeof (uint16_t),
	    bufl - sizeof (uint16_t));
	bkmem_free(buf, IPCC_MAX_DATA_SIZE);

	if (ret != Z_OK) {
		bipcc_printf("ipcc: decompression failed: %d\n", ret);
		bkmem_free(dst, dstlen);
		return (-1);
	}

	if (len != dstlen) {
		bipcc_printf("ipcc: decompressed length less than expected "
		    "(0x%zx < 0x%zx)\n", len, dstlen);
		bkmem_free(dst, dstlen);
		return (-1);
	}

	bipcc_printf("ipcc: decompressed to 0x%lx bytes\n", len);

	file = bkmem_alloc(sizeof (struct ipcc_file));
	file->if_key = key;
	file->if_fd = filedes++;
	file->if_off = 0;
	file->if_data = dst;
	file->if_size = len;

	add_open_file(file);

	return (file->if_fd);
}

static int
bipcc_close(int fd)
{
	struct ipcc_file *file;

	file = find_open_file(fd);
	if (file == NULL)
		return (-1);

	remove_open_file(file);

	bkmem_free(file->if_data, file->if_size);
	bkmem_free(file, sizeof (struct ipcc_file));

	return (0);
}

static void
bipcc_closeall(int flag __unused)
{
	struct ipcc_file *file;

	while (!SLIST_EMPTY(&open_files)) {
		file = SLIST_FIRST(&open_files);

		VERIFY0(bipcc_close(file->if_fd));
	}
}

static ssize_t
bipcc_read(int fd, caddr_t buf, size_t size)
{
	struct ipcc_file *file;

	file = find_open_file(fd);
	if (file == NULL)
		return (-1);

	if (file->if_off + size > file->if_size)
		size = file->if_size - file->if_off;

	if (size == 0)
		return (0);

	bcopy((void *)((uintptr_t)file->if_data + file->if_off), buf, size);

	file->if_off += size;

	return (size);
}

static off_t
bipcc_lseek(int fd, off_t addr, int whence)
{
	struct ipcc_file *file;

	file = find_open_file(fd);
	if (file == NULL)
		return (-1);

	switch (whence) {
		case SEEK_CUR:
			file->if_off += addr;
			break;
		case SEEK_SET:
			file->if_off = addr;
			break;
		case SEEK_END:
			file->if_off = file->if_size;
			break;
		default:
			bipcc_printf("lseek(): invalid whence value %d\n",
			    whence);
			return (-1);
	}

	return (0);
}

static int
bipcc_fstat(int fd, struct bootstat *bsp)
{
	struct ipcc_file *file;

	file = find_open_file(fd);
	if (file == NULL)
		return (-1);

	bsp->st_dev = 1;
	bsp->st_ino = file->if_key;
	bsp->st_mode = 0444;
	bsp->st_nlink = 1;
	bsp->st_uid = bsp->st_gid = 0;
	bsp->st_rdev = 0;
	bsp->st_size = file->if_size;
	bsp->st_blksize = 1;
	bsp->st_blocks = file->if_size;
	(void) strcpy(bsp->st_fstype, "bootfs");

	return (0);
}

struct boot_fs_ops bbootfs_ops = {
	.fsw_name		= "bootfs_ipcc",
	.fsw_mountroot		= bipcc_mountroot,
	.fsw_unmountroot	= bipcc_unmountroot,
	.fsw_open		= bipcc_open,
	.fsw_close		= bipcc_close,
	.fsw_closeall		= bipcc_closeall,
	.fsw_read		= bipcc_read,
	.fsw_lseek		= bipcc_lseek,
	.fsw_fstat		= bipcc_fstat,
};
