// Copyright 2024-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>

#include <driver/gpio.h>

#include "bk_partition.h"
#include "bk_posix.h"
#include "boot_video_fs.h"
#include "boot_video_priv.h"

#if CONFIG_SDCARD
#include <driver/sd_card.h>
#endif

#define BOOT_VIDEO_FS_UNMOUNT_SETTLE_MS   (100U)

/* --- SD-card cold-boot "virtual re-plug" (hardware-workaround) parameters ---
 *
 * Symptom: on cold power-on the SD card must be physically unplugged/replugged
 * before it will mount; every boot fails CMD8 otherwise.
 *
 * Cause: on this board the card rail is always powered (VBAT -> VDD SD/QSPI, no
 * GPIO-controlled SD LDO), and the SDIO0 lines CLK/CMD/DAT0 (GPIO2/3/4) carry
 * pull-ups to 3.3V. Those pull-ups can parasitically feed the card through its
 * I/O ESD diodes during power-up, latching it in an indeterminate state so it
 * ignores CMD0/CMD8 and never sees a clean power-on-reset.
 *
 * Fix: disable the CLK/CMD/DAT0 pull-ups (bk_gpio_disable_pull preserves the
 * SDIO mux) so the card discharges below its POR threshold, hold long enough for
 * the rails to bleed off, then restore the pull-ups before mount. The discharge
 * time is board-dependent and exposed as CONFIG_BOOT_VIDEO_SD_DISCHARGE_MS. */
#define BOOT_VIDEO_SD_CLK_GPIO             (GPIO_2)
#define BOOT_VIDEO_SD_CMD_GPIO             (GPIO_3)
#define BOOT_VIDEO_SD_DAT0_GPIO            (GPIO_4)
#ifndef CONFIG_BOOT_VIDEO_SD_DISCHARGE_MS
#define CONFIG_BOOT_VIDEO_SD_DISCHARGE_MS (1500)
#endif
#define BOOT_VIDEO_SD_DISCHARGE_MS         ((uint32_t)CONFIG_BOOT_VIDEO_SD_DISCHARGE_MS)
#define BOOT_VIDEO_SD_PULLUP_SETTLE_MS     (30U)

typedef struct
{
    const char *mount_path;   /* e.g. /sd0, /if0 */
    const char *device_name;  /* FATFS_DEV_* */
    bool        mounted;
    bool        auto_mounted; /* mounted implicitly by boot_video_play() */
} boot_video_fs_state_t;

static boot_video_fs_state_t s_fs_state[BOOT_VIDEO_FS_MAX] = {
    [BOOT_VIDEO_FS_SD] = {
        .mount_path  = VFS_SD_0_PATITION_0,
        .device_name = FATFS_DEV_SDCARD,
    },
    [BOOT_VIDEO_FS_INTERNAL_FLASH] = {
        .mount_path  = VFS_INTERNAL_FLASH_PATITION_0,
        .device_name = FATFS_DEV_FLASH,
    },
};

static void boot_video_fs_reset_hw(boot_video_fs_t fs)
{
#if CONFIG_SDCARD
    if (fs == BOOT_VIDEO_FS_SD)
    {
        (void)bk_sd_card_deinit();
    }
#else
    (void)fs;
#endif
}

void boot_video_fs_sd_cold_boot_reset(boot_video_fs_t fs)
{
#if CONFIG_SDCARD
    /* Only SD needs this, and only when the card is not already mounted (do not
     * disturb a filesystem the upper layer brought up on purpose). */
    if (fs != BOOT_VIDEO_FS_SD || (fs >= 0 && fs < BOOT_VIDEO_FS_MAX && s_fs_state[fs].mounted))
    {
        return;
    }

    BOOT_VIDEO_LOGI("%s: SD virtual re-plug (pull-off + discharge %ums)\n",
                    __func__, (unsigned)BOOT_VIDEO_SD_DISCHARGE_MS);

    /* 1) Emulate "unplug": drop the SDIO0 pull-ups so the card loses its bias. */
    (void)bk_gpio_disable_pull(BOOT_VIDEO_SD_CLK_GPIO);
    (void)bk_gpio_disable_pull(BOOT_VIDEO_SD_CMD_GPIO);
    (void)bk_gpio_disable_pull(BOOT_VIDEO_SD_DAT0_GPIO);

    /* 2) Let the card discharge below its power-on-reset threshold. */
    rtos_delay_milliseconds(BOOT_VIDEO_SD_DISCHARGE_MS);

    /* 3) Emulate "replug": restore the pull-ups and let them settle before mount. */
    (void)bk_gpio_pull_up(BOOT_VIDEO_SD_CLK_GPIO);
    (void)bk_gpio_pull_up(BOOT_VIDEO_SD_CMD_GPIO);
    (void)bk_gpio_pull_up(BOOT_VIDEO_SD_DAT0_GPIO);
    rtos_delay_milliseconds(BOOT_VIDEO_SD_PULLUP_SETTLE_MS);
#else
    (void)fs;
#endif
}

bk_err_t boot_video_fs_from_path(const char *path, boot_video_fs_t *out_fs)
{
    if (path == NULL || out_fs == NULL)
    {
        return BK_ERR_PARAM;
    }

    for (int i = 0; i < BOOT_VIDEO_FS_MAX; i++)
    {
        const char *prefix = s_fs_state[i].mount_path;
        size_t len = os_strlen(prefix);
        if (os_strncmp(path, prefix, len) == 0 &&
            (path[len] == '/' || path[len] == '\0'))
        {
            *out_fs = (boot_video_fs_t)i;
            return BK_OK;
        }
    }

    BOOT_VIDEO_LOGE("%s: unrecognized path prefix: %s\n", __func__, path);
    return BK_ERR_PARAM;
}

bk_err_t boot_video_fs_mount(boot_video_fs_t fs)
{
    if (fs < 0 || fs >= BOOT_VIDEO_FS_MAX)
    {
        return BK_ERR_PARAM;
    }

    boot_video_fs_state_t *st = &s_fs_state[fs];
    if (st->mounted)
    {
        return BK_OK;
    }

    struct bk_fatfs_partition partition;
    os_memset(&partition, 0, sizeof(partition));
    partition.part_type = FATFS_DEVICE;
    partition.part_dev.device_name = st->device_name;
    partition.mount_path = st->mount_path;

    int ret = mount("SOURCE_NONE", partition.mount_path, "fatfs", 0, &partition);
    if (ret == BK_OK)
    {
        st->mounted = true;
        BOOT_VIDEO_LOGI("%s: mounted %s (%s)\n", __func__, st->mount_path, st->device_name);
        return BK_OK;
    }

    boot_video_fs_reset_hw(fs);
    BOOT_VIDEO_LOGE("%s: mount %s failed, ret=%d\n", __func__, st->mount_path, ret);
    return ret;
}

bk_err_t boot_video_fs_unmount(boot_video_fs_t fs)
{
    if (fs < 0 || fs >= BOOT_VIDEO_FS_MAX)
    {
        return BK_ERR_PARAM;
    }

    boot_video_fs_state_t *st = &s_fs_state[fs];
    if (!st->mounted)
    {
        return BK_OK;
    }

    int ret = umount(st->mount_path);
    st->mounted = false;
    st->auto_mounted = false;
    if (ret != BK_OK)
    {
        BOOT_VIDEO_LOGE("%s: umount %s failed, ret=%d\n", __func__, st->mount_path, ret);
    }
    else
    {
        BOOT_VIDEO_LOGI("%s: unmounted %s\n", __func__, st->mount_path);
    }

    /* Reset the backing device so a subsequent mount() re-initializes cleanly. */
    boot_video_fs_reset_hw(fs);
    rtos_delay_milliseconds(BOOT_VIDEO_FS_UNMOUNT_SETTLE_MS);
    return ret;
}

bool boot_video_fs_is_mounted(boot_video_fs_t fs)
{
    if (fs < 0 || fs >= BOOT_VIDEO_FS_MAX)
    {
        return false;
    }
    return s_fs_state[fs].mounted;
}

bool boot_video_fs_was_auto_mounted(boot_video_fs_t fs)
{
    if (fs < 0 || fs >= BOOT_VIDEO_FS_MAX)
    {
        return false;
    }
    return s_fs_state[fs].auto_mounted;
}

void boot_video_fs_mark_auto_mounted(boot_video_fs_t fs, bool auto_mounted)
{
    if (fs < 0 || fs >= BOOT_VIDEO_FS_MAX)
    {
        return;
    }
    s_fs_state[fs].auto_mounted = auto_mounted;
}
