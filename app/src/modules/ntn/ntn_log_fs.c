/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_backend.h>

/* The console keeps the compile-time level (debug), the file system backend only what is
 * useful after the fact. Needs CONFIG_LOG_RUNTIME_FILTERING.
 */
static int ntn_log_fs_filter(void)
{
	const struct log_backend *backend = log_backend_get_by_name("log_backend_fs");

	if (backend == NULL) {
		return 0;
	}

	for (uint32_t src = 0; src < log_src_cnt_get(Z_LOG_LOCAL_DOMAIN_ID); src++) {
		log_filter_set(backend, Z_LOG_LOCAL_DOMAIN_ID, src, CONFIG_APP_NTN_LOG_FS_LEVEL);
	}

	return 0;
}

SYS_INIT(ntn_log_fs_filter, APPLICATION, 99);
