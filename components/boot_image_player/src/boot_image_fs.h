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

#pragma once

#include <common/bk_err.h>
#include "boot_image_player.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Map a VFS path to the filesystem that backs it, by prefix.
 * @param path   VFS path, e.g. "/sd0/boot.jpg" or "/if0/boot.jpg".
 * @param out_fs Output filesystem enum.
 * @return BK_OK if recognized, BK_ERR_PARAM otherwise.
 */
bk_err_t boot_image_fs_from_path(const char *path, boot_image_fs_t *out_fs);

/**
 * @brief Whether the given filesystem was auto-mounted by boot_image_show()
 *        (i.e. not explicitly mounted by the upper layer beforehand).
 */
bool boot_image_fs_was_auto_mounted(boot_image_fs_t fs);
void boot_image_fs_mark_auto_mounted(boot_image_fs_t fs, bool auto_mounted);

/**
 * @brief SD-card cold-boot "virtual re-plug" — a firmware workaround for a board
 *        hardware limitation, to be called once right before the first mount.
 *
 * On this board the TF/SD card shares the system power rail with no dedicated
 * power switch, so it stays powered across a warm reset. The SDIO0 pull-ups
 * (CLK/CMD/DAT0) then keep the card partially biased on a cold boot, preventing
 * a clean power-on-reset and causing the first enumeration/mount to fail.
 *
 * This routine emulates a physical unplug/replug entirely in firmware: it drops
 * the three pull-ups so the card discharges below its POR threshold, waits
 * CONFIG_BOOT_IMAGE_SD_DISCHARGE_MS, then restores the pull-ups so the following
 * mount() sees a freshly reset card.
 *
 * Safe to call unconditionally: it is a no-op for non-SD filesystems, for an
 * already-mounted card, and when SD support (CONFIG_SDCARD) is disabled.
 *
 * @param fs Target filesystem (only BOOT_IMAGE_FS_SD is acted upon).
 */
void boot_image_fs_sd_cold_boot_reset(boot_image_fs_t fs);

#ifdef __cplusplus
}
#endif
