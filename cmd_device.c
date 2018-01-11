#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libbcachefs/bcachefs_ioctl.h"
#include "libbcachefs/super-io.h"
#include "cmds.h"
#include "libbcachefs.h"
#include "libbcachefs/opts.h"
#include "tools-util.h"

/* This code belongs under show_fs */
#if 0

struct bcache_dev {
	unsigned	nr;
	const char	*name;

	unsigned	has_metadata;
	unsigned	has_data;
	const char	*state;
	unsigned	tier;

	u64		bucket_size;
	u64		first_bucket;
	u64		nbuckets;

	/* XXX: dirty != used, it doesn't count metadata */
	u64		bytes_dirty;
};

static struct bcache_dev fill_dev(const char *dev_name, unsigned nr, int dir)
{
	return (struct bcache_dev) {
		.nr		= nr,
		.name		= dev_name,

		.has_metadata	= read_file_u64(dir, "has_metadata"),
		.has_data	= read_file_u64(dir, "has_data"),
		.state		= read_file_str(dir, "state"),
		.tier		= read_file_u64(dir, "tier"),

		.bucket_size	= read_file_u64(dir, "bucket_size_bytes"),
		.first_bucket	= read_file_u64(dir, "first_bucket"),
		.nbuckets	= read_file_u64(dir, "nbuckets"),
		.bytes_dirty	= read_file_u64(dir, "dirty_bytes"),
	};
}

static void show_dev(struct bcache_dev *dev)
{
	u64 capacity = (dev->nbuckets - dev->first_bucket) *
		dev->bucket_size;
	/*
	 * XXX: show fragmentation information, cached/dirty information
	 */

	printf("Device %u (/dev/%s):\n"
	       "    Has metadata:\t%u\n"
	       "    Has dirty data:\t%u\n"
	       "    State:\t\t%s\n"
	       "    Tier:\t\t%u\n"
	       "    Size:\t\t%llu\n"
	       "    Used:\t\t%llu\n"
	       "    Free:\t\t%llu\n"
	       "    Use%%:\t\t%llu\n",
	       dev->nr, dev->name,
	       dev->has_metadata,
	       dev->has_data,
	       dev->state,
	       dev->tier,
	       capacity,
	       dev->bytes_dirty,
	       capacity - dev->bytes_dirty,
	       (dev->bytes_dirty * 100) / capacity);
}

int cmd_device_show(int argc, char *argv[])
{
	int human_readable = 0;
	NihOption opts[] = {
	//	{ int shortoption, char *longoption, char *help, NihOptionGroup, char *argname, void *value, NihOptionSetter}

		{ 'h',	"human-readable",		N_("print sizes in powers of 1024 (e.g., 1023M)"),
			NULL, NULL,	&human_readable,	NULL},
		NIH_OPTION_LAST
	};
	char **args = bch_nih_init(argc, argv, opts);

	if (argc != 2)
		die("Please supply a single device");

	struct bchfs_handle fs = bcache_fs_open(argv[1]);
	struct dirent *entry;

	struct bcache_dev devices[256];
	unsigned i, j, nr_devices = 0, nr_active_tiers = 0;

	unsigned tiers[BCH_TIER_MAX]; /* number of devices in each tier */
	memset(tiers, 0, sizeof(tiers));

	while ((entry = readdir(fs.sysfs))) {
		unsigned nr;
		int pos = 0;

		sscanf(entry->d_name, "cache%u%n", &nr, &pos);
		if (pos != strlen(entry->d_name))
			continue;

		char link[PATH_MAX];
		if (readlinkat(dirfd(fs.sysfs), entry->d_name,
			       link, sizeof(link)) < 0)
			die("readlink error: %m\n");

		char *dev_name = basename(dirname(link));

		int fd = xopenat(dirfd(fs.sysfs), entry->d_name, O_RDONLY);

		devices[nr_devices] = fill_dev(strdup(dev_name), nr, fd);
		tiers[devices[nr_devices].tier]++;
		nr_devices++;

		close(fd);
	}

	for (i = 0; i < BCH_TIER_MAX; i++)
		if (tiers[i])
			nr_active_tiers++;

	/* Print out devices sorted by tier: */
	bool first = true;

	for (i = 0; i < BCH_TIER_MAX; i++) {
		if (!tiers[i])
			continue;

		if (nr_active_tiers > 1) {
			if (!first)
				printf("\n");
			first = false;
			printf("Tier %u:\n\n", i);
		}

		for (j = 0; j < nr_devices; j++) {
			if (devices[j].tier != i)
				continue;

			if (!first)
				printf("\n");
			first = false;
			show_dev(&devices[j]);
		}
	}

	return 0;
}
#endif

static void disk_ioctl(const char *fs, const char *dev, int cmd, int flags)
{
	struct bch_ioctl_disk i = { .flags = flags, };

	if (!kstrtoull(dev, 10, &i.dev))
		i.flags |= BCH_BY_INDEX;
	else
		i.dev = (u64) dev;

	xioctl(bcache_fs_open(fs).ioctl_fd, cmd, &i);
}

static void device_add_usage(void)
{
	puts("bcachefs device add - add a device to an existing filesystem\n"
	     "Usage: bcachefs device add [OPTION]... filesystem device\n"
	     "\n"
	     "Options:\n"
	     "      --fs_size=size          Size of filesystem on device\n"
	     "      --bucket=size           Bucket size\n"
	     "      --discard               Enable discards\n"
	     "  -t, --tier=#                Higher tier (e.g. 1) indicates slower devices\n"
	     "  -f, --force                 Use device even if it appears to already be formatted\n"
	     "  -h, --help                  Display this help and exit\n"
	     "\n"
	     "Report bugs to <linux-bcache@vger.kernel.org>");
}

int cmd_device_add(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "fs_size",		required_argument,	NULL, 'S' },
		{ "bucket",		required_argument,	NULL, 'B' },
		{ "discard",		no_argument,		NULL, 'D' },
		{ "tier",		required_argument,	NULL, 't' },
		{ "force",		no_argument,		NULL, 'f' },
		{ NULL }
	};
	struct format_opts format_opts	= format_opts_default();
	struct dev_opts dev_opts	= dev_opts_default();
	bool force = false;
	int opt;

	while ((opt = getopt_long(argc, argv, "t:fh",
				  longopts, NULL)) != -1)
		switch (opt) {
		case 'S':
			if (bch2_strtoull_h(optarg, &dev_opts.size))
				die("invalid filesystem size");

			dev_opts.size >>= 9;
			break;
		case 'B':
			dev_opts.bucket_size =
				hatoi_validate(optarg, "bucket size");
			break;
		case 'D':
			dev_opts.discard = true;
			break;
		case 't':
			if (kstrtouint(optarg, 10, &dev_opts.tier) ||
			    dev_opts.tier >= BCH_TIER_MAX)
				die("invalid tier");
			break;
		case 'f':
			force = true;
			break;
		case 'h':
			device_add_usage();
			exit(EXIT_SUCCESS);
		}
	args_shift(optind);

	if (argc != 2)
		die("Please supply a filesystem and a device to add");

	struct bchfs_handle fs = bcache_fs_open(arg_pop());

	dev_opts.path = arg_pop();
	dev_opts.fd = open_for_format(dev_opts.path, force);

	format_opts.block_size	=
		read_file_u64(fs.sysfs_fd, "block_size") >> 9;
	format_opts.btree_node_size =
		read_file_u64(fs.sysfs_fd, "btree_node_size") >> 9;

	struct bch_sb *sb = bch2_format(format_opts, &dev_opts, 1);
	free(sb);
	fsync(dev_opts.fd);
	close(dev_opts.fd);

	bchu_disk_add(fs, dev_opts.path);
	return 0;
}

static void device_remove_usage(void)
{
	puts("bcachefs device_remove - remove a device from a filesystem\n"
	     "Usage: bcachefs device remove filesystem device\n"
	     "\n"
	     "Options:\n"
	     "  -f, --force		    Force removal, even if some data\n"
	     "                              couldn't be migrated\n"
	     "      --force-metadata	    Force removal, even if some metadata\n"
	     "                              couldn't be migrated\n"
	     "  -h, --help                  display this help and exit\n"
	     "Report bugs to <linux-bcache@vger.kernel.org>");
	exit(EXIT_SUCCESS);
}

int cmd_device_remove(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "force",		0, NULL, 'f' },
		{ "force-metadata",	0, NULL, 'F' },
		{ "help",		0, NULL, 'h' },
		{ NULL }
	};
	int opt, flags = BCH_FORCE_IF_DEGRADED;

	while ((opt = getopt_long(argc, argv, "fh", longopts, NULL)) != -1)
		switch (opt) {
		case 'f':
			flags |= BCH_FORCE_IF_DATA_LOST;
			break;
		case 'F':
			flags |= BCH_FORCE_IF_METADATA_LOST;
			break;
		case 'h':
			device_remove_usage();
		}
	args_shift(optind);

	char *fs = arg_pop();
	if (!fs)
		die("Please supply a filesystem");

	char *dev = arg_pop();
	if (!dev)
		die("Please supply a device to remove");

	if (argc)
		die("too many arguments");

	disk_ioctl(fs, dev, BCH_IOCTL_DISK_REMOVE, flags);
	return 0;
}

static void device_online_usage(void)
{
	puts("bcachefs device online - readd a device to a running filesystem\n"
	     "Usage: bcachefs device online [OPTION]... filesystem device\n"
	     "\n"
	     "Options:\n"
	     "  -h, --help                  Display this help and exit\n"
	     "\n"
	     "Report bugs to <linux-bcache@vger.kernel.org>");
}

int cmd_device_online(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "h")) != -1)
		switch (opt) {
		case 'h':
			device_online_usage();
			exit(EXIT_SUCCESS);
		}

	if (argc - optind != 2)
		die("Please supply a filesystem and a device");

	disk_ioctl(argv[optind], argv[optind + 1], BCH_IOCTL_DISK_ONLINE, 0);
	return 0;
}

static void device_offline_usage(void)
{
	puts("bcachefs device offline - take a device offline, without removing it\n"
	     "Usage: bcachefs device offline [OPTION]... filesystem device\n"
	     "\n"
	     "Options:\n"
	     "  -f, --force		    Force, if data redundancy will be degraded\n"
	     "  -h, --help                  Display this help and exit\n"
	     "\n"
	     "Report bugs to <linux-bcache@vger.kernel.org>");
}

int cmd_device_offline(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "force",		0, NULL, 'f' },
		{ NULL }
	};
	int opt, flags = 0;

	while ((opt = getopt_long(argc, argv, "fh",
				  longopts, NULL)) != -1)
		switch (opt) {
		case 'f':
			flags |= BCH_FORCE_IF_DEGRADED;
			break;
		case 'h':
			device_offline_usage();
			exit(EXIT_SUCCESS);
		}

	if (argc - optind != 2)
		die("Please supply a filesystem and a device");

	disk_ioctl(argv[optind], argv[optind + 1],
		   BCH_IOCTL_DISK_OFFLINE, flags);
	return 0;
}

static void device_evacuate_usage(void)
{
	puts("bcachefs device evacuate - move data off of a given device\n"
	     "Usage: bcachefs device evacuate [OPTION]... filesystem device\n"
	     "\n"
	     "Options:\n"
	     "  -h, --help                  Display this help and exit\n"
	     "\n"
	     "Report bugs to <linux-bcache@vger.kernel.org>");
}

int cmd_device_evacuate(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "h")) != -1)
		switch (opt) {
		case 'h':
			device_evacuate_usage();
			exit(EXIT_SUCCESS);
		}

	if (argc - optind != 2)
		die("Please supply a filesystem and a device");

	disk_ioctl(argv[optind], argv[optind + 1], BCH_IOCTL_DISK_EVACUATE, 0);
	return 0;
}

static void device_set_state_usage(void)
{
	puts("bcachefs device set-state\n"
	     "Usage: bcachefs device set-state filesystem device new-state\n"
	     "\n"
	     "Options:\n"
	     "  -f, --force		    Force, if data redundancy will be degraded\n"
	     "  -h, --help                  display this help and exit\n"
	     "Report bugs to <linux-bcache@vger.kernel.org>");
	exit(EXIT_SUCCESS);
}

int cmd_device_set_state(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "force",			0, NULL, 'f' },
		{ "help",			0, NULL, 'h' },
		{ NULL }
	};
	int opt, flags = 0;

	while ((opt = getopt_long(argc, argv, "fh", longopts, NULL)) != -1)
		switch (opt) {
		case 'f':
			flags |= BCH_FORCE_IF_DEGRADED;
			break;
		case 'h':
			device_set_state_usage();
		}

	if (argc - optind != 3)
		die("Please supply a filesystem, device and state");

	struct bchfs_handle fs = bcache_fs_open(argv[optind]);

	bchu_disk_set_state(fs, argv[optind + 1],
			    read_string_list_or_die(argv[optind + 2],
					bch2_dev_state, "device state"),
			    flags);
	return 0;
}

static void device_resize_usage(void)
{
	puts("bcachefs device resize \n"
	     "Usage: bcachefs device resize device [ size ]\n"
	     "\n"
	     "Options:\n"
	     "  -h, --help                  display this help and exit\n"
	     "Report bugs to <linux-bcache@vger.kernel.org>");
	exit(EXIT_SUCCESS);
}

int cmd_device_resize(int argc, char *argv[])
{
	static const struct option longopts[] = {
		{ "help",			0, NULL, 'h' },
		{ NULL }
	};
	u64 size;
	int opt;

	while ((opt = getopt_long(argc, argv, "h", longopts, NULL)) != -1)
		switch (opt) {
		case 'h':
			device_resize_usage();
		}
	args_shift(optind);

	char *dev = arg_pop();
	if (!dev)
		die("Please supply a device to resize");

	int dev_fd = xopen(dev, O_RDONLY);

	char *size_arg = arg_pop();
	if (!size_arg)
		size = get_size(dev, dev_fd);
	else if (bch2_strtoull_h(size_arg, &size))
		die("invalid size");

	size >>= 9;

	if (argc)
		die("Too many arguments");

	struct stat dev_stat = xfstat(dev_fd);

	char *mount = dev_to_mount(dev);
	if (mount) {
		if (!S_ISBLK(dev_stat.st_mode))
			die("%s is mounted but isn't a block device?!", dev);

		printf("Doing online resize of %s\n", dev);

		struct bchfs_handle fs = bcache_fs_open(mount);

		unsigned idx = bchu_disk_get_idx(fs, dev_stat.st_rdev);

		struct bch_sb *sb = bchu_read_super(fs, -1);
		if (idx >= sb->nr_devices)
			die("error reading superblock: dev idx >= sb->nr_devices");

		struct bch_sb_field_members *mi = bch2_sb_get_members(sb);
		if (!mi)
			die("error reading superblock: no member info");

		/* could also just read this out of sysfs... meh */
		struct bch_member *m = mi->members + idx;

		u64 nbuckets = size / le16_to_cpu(m->bucket_size);

		printf("resizing %s to %llu buckets\n", dev, nbuckets);
		bchu_disk_resize(fs, idx, nbuckets);
	} else {
		printf("Doing offline resize of %s\n", dev);

		struct bch_fs *c = bch2_fs_open(&dev, 1, bch2_opts_empty());
		if (IS_ERR(c))
			die("error opening %s: %s", dev, strerror(-PTR_ERR(c)));

		struct bch_dev *ca, *resize = NULL;
		unsigned i;

		for_each_online_member(ca, c, i) {
			if (resize)
				die("confused: more than one online device?");
			resize = ca;
			percpu_ref_get(&resize->io_ref);
		}

		u64 nbuckets = size / le16_to_cpu(resize->mi.bucket_size);

		printf("resizing %s to %llu buckets\n", dev, nbuckets);
		int ret = bch2_dev_resize(c, resize, nbuckets);
		if (ret)
			fprintf(stderr, "resize error: %s\n", strerror(-ret));

		percpu_ref_put(&resize->io_ref);
		bch2_fs_stop(c);
	}
	return 0;
}
