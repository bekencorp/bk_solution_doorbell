// On-demand U-disk (USB MSC host) mount / umount / write-read verify CLI.
// Lives in smart_lock (not multimedia_device_service) so it can call
// doorbell_mm_service_vote(): a mounted U-disk votes MM_STATUS_UDISK busy, which
// keeps the doorbell_lp keepalive path from powering the AP off, same as camera.

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>
#include "cli.h"

#if CONFIG_USBH_MSC
#include <components/usb_types.h>
#include <components/usbh_hub_multiple_classes_api.h>
#include <components/cherryusb/usbh_msc.h>
// bk_vfs_* + O_ flags directly, not bk_posix.h: its read/write/close macros clash
// with lwip sockets.h (pulled in via doorbell_cmd.h).
#include <posix/fcntl.h>
#include "bk_vfs.h"
#include "bk_filesystem.h"
#include "bk_partition.h"
#include "doorbell_cmd.h"

#define TAG "db-udisk"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define UDISK_READY_TIMEOUT_MS 5000
#define UDISK_READY_POLL_MS    50

#define UDISK_TEST_FILE  VFS_USB_0_PATITION_0 "/udisk_test.bin"
#define UDISK_TEST_SIZE  (16 * 1024)

static void udisk_vote_busy(bool busy)
{
#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    // busy => block the keepalive idle countdown; idle => allow low-power again.
    doorbell_mm_service_vote(MM_STATUS_UDISK_BIT, busy);
#else
    (void)busy;
#endif
}

static void udisk_host_power_down(void)
{
    for (uint8_t port = USB_HUB_PORT_1; port <= CONFIG_USBHOST_HUB_MAX_EHPORTS; port++)
        bk_usbh_hub_multiple_devices_power_down(USB_HOST_MODE, port, USB_MSD_DEVICE);
}

// Vote busy, power the host up, wait for the disk, mount it at /ud0.
// Returns 0 on success; on any failure it logs, rolls back (power down + release
// vote) and returns -1.
static int udisk_do_mount(void)
{
    struct bk_fatfs_partition part = {0};

    udisk_vote_busy(true);

    for (uint8_t port = USB_HUB_PORT_1; port <= CONFIG_USBHOST_HUB_MAX_EHPORTS; port++)
        bk_usbh_hub_multiple_devices_power_on(USB_HOST_MODE, port, USB_MSD_DEVICE);

    for (int waited = 0; !usbh_ms_media_get_status(); waited += UDISK_READY_POLL_MS)
    {
        if (waited >= UDISK_READY_TIMEOUT_MS)
        {
            LOGI("[udisk] no disk / enum timeout\r\n");
            udisk_host_power_down();
            udisk_vote_busy(false);
            return -1;
        }
        rtos_delay_milliseconds(UDISK_READY_POLL_MS);
    }

    part.part_type            = FATFS_DEVICE;
    part.part_dev.device_name = FATFS_DEV_UDISK;
    part.mount_path           = VFS_USB_0_PATITION_0;
    if (bk_vfs_mount("SOURCE_NONE", part.mount_path, "fatfs", 0, &part) != 0)
    {
        LOGI("[udisk] mount %s failed\r\n", VFS_USB_0_PATITION_0);
        udisk_host_power_down();
        udisk_vote_busy(false);
        return -1;
    }
    return 0;
}

// Unmount /ud0, power the host down, release the keep-awake vote.
static int udisk_do_umount(void)
{
    int ret = bk_vfs_umount(VFS_USB_0_PATITION_0);
    udisk_host_power_down();
    udisk_vote_busy(false);
    return ret;
}

// Write a byte pattern to the mounted /ud0, read it back, check it against the
// pattern (so one buffer is enough), report PASS/FAIL and delete the test file.
static void udisk_run_verify_test(void)
{
    uint8_t *buf = os_malloc(UDISK_TEST_SIZE);
    int fd, ok = 0;

    if (!buf)
    {
        LOGI("[udisk] test: os_malloc failed\r\n");
        return;
    }
    // Pattern mixes in the high index bits so it does not repeat every 256 bytes:
    // a whole-block offset in the read-back path would otherwise still compare equal.
    for (uint32_t i = 0; i < UDISK_TEST_SIZE; i++)
        buf[i] = (uint8_t)(i ^ (i >> 8));

    fd = bk_vfs_open(UDISK_TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0)
    {
        ok = (bk_vfs_write(fd, buf, UDISK_TEST_SIZE) == (int)UDISK_TEST_SIZE);
        bk_vfs_close(fd);
    }

    if (ok)
    {
        os_memset(buf, 0, UDISK_TEST_SIZE);
        ok = 0;
        fd = bk_vfs_open(UDISK_TEST_FILE, O_RDONLY);
        if (fd >= 0)
        {
            ok = (bk_vfs_read(fd, buf, UDISK_TEST_SIZE) == (int)UDISK_TEST_SIZE);
            bk_vfs_close(fd);
        }
        for (uint32_t i = 0; ok && i < UDISK_TEST_SIZE; i++)
            ok = (buf[i] == (uint8_t)(i ^ (i >> 8)));
    }

    LOGI("[udisk] test: verify %s (%u bytes)\r\n", ok ? "PASS" : "FAIL", UDISK_TEST_SIZE);
    bk_vfs_unlink(UDISK_TEST_FILE);
    os_free(buf);
}

void cli_doorbell_udisk_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (argc >= 2 && os_strcmp(argv[1], "umount") == 0)
    {
        int ret = udisk_do_umount();
        LOGI("[udisk] umount %s ret=%d\r\n", VFS_USB_0_PATITION_0, ret);
        return;
    }

    if (argc >= 2 && os_strcmp(argv[1], "test") == 0)
    {
        // Self-contained: mount -> write/read/verify -> umount.
        if (udisk_do_mount() != 0)
            return;
        udisk_run_verify_test();
        (void)udisk_do_umount();
        return;
    }

    if (argc >= 2 && os_strcmp(argv[1], "mount") == 0)
    {
        if (udisk_do_mount() == 0)
            LOGI("[udisk] mount %s ret=0\r\n", VFS_USB_0_PATITION_0);
        return;
    }

    LOGI("usage: udisk mount|umount|test\r\n");
}

// Linker-section auto-register (doorbell_keepalive_cli_init is commented out on *_lp).
COMPONENTS_CLI_CMD_EXPORT
static const struct cli_command s_udisk_cmds[] = {
    {"udisk", "udisk mount|umount|test  (USB host + /ud0)", cli_doorbell_udisk_cmd},
};

#endif /* CONFIG_USBH_MSC */
