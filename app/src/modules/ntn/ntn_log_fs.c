/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <pm_config.h>
#include <zephyr/init.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_backend.h>

LOG_MODULE_REGISTER(ntn_log_fs, CONFIG_APP_NTN_LOG_LEVEL);

/* The littlefs of the file system log backend lives in the Partition Manager's external_flash
 * partition, the free space after the modem trace. It is mounted here, by partition id,
 * because the devicetree partition handles do not map to it with the Partition Manager.
 * Messages logged before the mount are not stored.
 */
FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(log_lfs, 4, 16, 16, 64, 32);

static struct fs_mount_t log_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &log_lfs,
	.storage_dev = (void *)PM_EXTERNAL_FLASH_ID,
	.mnt_point = "/lfs1",
};

/* The console keeps the compile-time level (debug), the file system backend only what is
 * useful after the fact. Needs CONFIG_LOG_RUNTIME_FILTERING.
 */
static void fs_log_filter(void)
{
	const struct log_backend *backend = log_backend_get_by_name("log_backend_fs");

	if (backend == NULL) {
		return;
	}

	for (uint32_t src = 0; src < log_src_cnt_get(Z_LOG_LOCAL_DOMAIN_ID); src++) {
		log_filter_set(backend, Z_LOG_LOCAL_DOMAIN_ID, src, CONFIG_APP_NTN_LOG_FS_LEVEL);
	}
}

static int ntn_log_fs_init(void)
{
	int err;

	log_lfs.cfg.block_cycles = 512;

	err = fs_mount(&log_mnt);
	if (err) {
		LOG_ERR("Mounting %s failed: %d", log_mnt.mnt_point, err);
		return 0;
	}

	fs_log_filter();
	LOG_INF("Log file system mounted at %s", log_mnt.mnt_point);

	return 0;
}

SYS_INIT(ntn_log_fs_init, APPLICATION, 0);
