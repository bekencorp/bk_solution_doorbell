#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>
#include <posix/fcntl.h>
#include "bk_vfs.h"
#include "bk_filesystem.h"
#include "bk_partition.h"
#include "bk_snapshot.h"
#include "doorbell_devices.h"

#define TAG "db-snap-sd"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#if CONFIG_SDCARD

static bool s_snapshot_sd_mounted = false;
static uint32_t s_snapshot_file_index = 0;

static bk_err_t doorbell_snapshot_sd_mount(void)
{
	int ret;

	if (s_snapshot_sd_mounted) {
		return BK_OK;
	}

	struct bk_fatfs_partition partition;

	os_memset(&partition, 0, sizeof(partition));
	partition.part_type = FATFS_DEVICE;
	partition.part_dev.device_name = FATFS_DEV_SDCARD;
	partition.mount_path = VFS_SD_0_PATITION_0;
	ret = bk_vfs_mount("SOURCE_NONE", partition.mount_path, "fatfs", 0, &partition);
	if (ret != BK_OK) {
		LOGE("sd mount failed ret=%d\r\n", ret);
		return BK_FAIL;
	}

	s_snapshot_sd_mounted = true;
	LOGI("sd mounted at %s\r\n", VFS_SD_0_PATITION_0);
	return BK_OK;
}

static bk_err_t doorbell_snapshot_sd_umount(void)
{
	int ret = BK_OK;

	if (!s_snapshot_sd_mounted) {
		return BK_OK;
	}

	ret = bk_vfs_umount(VFS_SD_0_PATITION_0);
	if (ret != BK_OK) {
		LOGE("sd umount failed ret=%d\r\n", ret);
		return BK_FAIL;
	}

	s_snapshot_sd_mounted = false;
	return BK_OK;
}

#endif

bk_err_t doorbell_snapshot_save_to_sd(const bk_snapshot_image_t *image, char *path_out,
				      uint32_t path_len)
{
#if CONFIG_SDCARD
	char path[64];
	int fd = -1;
	ssize_t written;
	bk_err_t ret;

	if (image == NULL || image->data == NULL || image->size == 0) {
		return BK_ERR_PARAM;
	}

	ret = doorbell_snapshot_sd_mount();
	if (ret != BK_OK) {
		return ret;
	}

	os_snprintf(path, sizeof(path), VFS_SD_0_PATITION_0 "/snap_%04u.jpg", s_snapshot_file_index++);
	fd = bk_vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
	if (fd < 0) {
		LOGE("open %s failed\r\n", path);
		ret = BK_FAIL;
		goto out;
	}

	written = bk_vfs_write(fd, image->data, image->size);
	if (written != (ssize_t)image->size) {
		LOGE("write %s failed written=%d size=%u\r\n", path, (int)written, image->size);
		ret = BK_FAIL;
		goto out;
	}

	(void)bk_vfs_fsync(fd);
	(void)bk_vfs_close(fd);
	fd = -1;

	if (path_out != NULL && path_len > 0) {
		os_strncpy(path_out, path, path_len - 1);
		path_out[path_len - 1] = '\0';
	}

	LOGI("saved to %s (%u bytes)\r\n", path, image->size);
	ret = BK_OK;

out:
	if (fd >= 0) {
		(void)bk_vfs_close(fd);
	}
	(void)doorbell_snapshot_sd_umount();
	return ret;
#else
	(void)image;
	(void)path_out;
	(void)path_len;
	LOGE("sd card disabled, enable CONFIG_SDCARD\r\n");
	return BK_ERR_NOT_SUPPORT;
#endif
}
