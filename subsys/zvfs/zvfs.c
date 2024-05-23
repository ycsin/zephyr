/* blah */

#include <zephyr/net/socket.h>
#include <zephyr/fs/fs.h>
#include <zephyr/zvfs.h>

static struct zvfs_file *zvfs_fd_to_file(int fd)
{
	void *obj = z_get_fd_obj(fd, NULL, EBADF);

	if (obj == NULL) {
		return NULL;
	}

	struct fd_entry *entry = CONTAINER_OF(obj, struct fd_entry, obj);

	return CONTAINER_OF(entry, struct zvfs_file, entry);
}

static int zvfs_file_to_fd(const struct zvfs_file *const file)
{
	void *obj = file->entry.obj;
	int fd = z_get_fd_by_obj_and_vtable(obj, NULL);

	return fd;
}

int zvfs_open(struct zvfs_file *fp, const char *file_name, int flags)
{
	int rc = 0;

	switch (fp->mode) {
	case ZVFS_MODE_FILE:
		fs_open(fp->entry.obj, file_name, (fs_mode_t)flags);
	break;
	default:
		rc = -EBADF;
	};

	return 0;
}
