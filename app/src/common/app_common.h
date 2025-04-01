/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _APP_COMMON_H_
#define _APP_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/util.h>  /* For Zephyr's utility macros, including MAX */
#if defined(CONFIG_MEMFAULT)
#include <memfault/panics/assert.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Handle fatal error.
 *  @param is_watchdog_timeout Boolean indicating if the macro was called upon a watchdog timeout.
 */
#define FATAL_ERROR_HANDLE(is_watchdog_timeout) do {				\
	LOG_PANIC();								\
	if (is_watchdog_timeout) {						\
		IF_ENABLED(CONFIG_MEMFAULT, (MEMFAULT_SOFTWARE_WATCHDOG()));	\
	}									\
	k_sleep(K_SECONDS(10));							\
	__ASSERT(false, "SEND_FATAL_ERROR() macro called");			\
} while (0)

/** @brief Macro used to handle fatal errors. */
#define SEND_FATAL_ERROR() FATAL_ERROR_HANDLE(0)

/** @brief Macro used to handle watchdog timeouts. */
#define SEND_FATAL_ERROR_WATCHDOG_TIMEOUT() FATAL_ERROR_HANDLE(1)

/* Helper macros for computing MAX with different numbers of arguments */
#define MAX_1(a1)			(a1)
#define MAX_2(a1, a2)			MAX(a1, a2)
#define MAX_3(a1, a2, a3)		MAX(MAX_2(a1, a2), a3)
#define MAX_4(a1, a2, a3, a4)		MAX(MAX_3(a1, a2, a3), a4)
#define MAX_5(a1, a2, a3, a4, a5)	MAX(MAX_4(a1, a2, a3, a4), a5)
#define MAX_6(a1, a2, a3, a4, a5, a6)	MAX(MAX_5(a1, a2, a3, a4, a5), a6)
#define MAX_7(a1, a2, a3, a4, a5, a6, a7) \
					MAX(MAX_6(a1, a2, a3, a4, a5, a6), a7)
#define MAX_8(a1, a2, a3, a4, a5, a6, a7, a8) \
					MAX(MAX_7(a1, a2, a3, a4, a5, a6, a7), a8)
#define MAX_9(a1, a2, a3, a4, a5, a6, a7, a8, a9) \
					MAX(MAX_8(a1, a2, a3, a4, a5, a6, a7, a8), a9)
#define MAX_10(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
					MAX(MAX_9(a1, a2, a3, a4, a5, a6, a7, a8, a9), a10)

#define SELECT_MAX_N(N)			CONCAT(MAX_, N)

/**
 * @brief Macro to compute the maximum of a list of numbers.
 * @param ... List of numbers to compute the maximum of.
 */
#define MAX_N(...) SELECT_MAX_N(NUM_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* _APP_COMMON_H_ */
