/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "ntn_led.h"

LOG_MODULE_REGISTER(ntn_led, CONFIG_APP_NTN_LOG_LEVEL);

#define BLINK_NORMAL_MS	500
#define BLINK_FAST_MS	150
#define BLINK_SLOW_MS	1000

/* Idle heartbeat: a short flash so a dark LED does not look like a dead board. */
#define HEARTBEAT_ON_MS		80
#define HEARTBEAT_PERIOD_MS	3000

/* The three channels of the single RGB LED (Thingy:91 X aliases led0..led2). */
enum rgb_channel {
	CH_RED,
	CH_GREEN,
	CH_BLUE,
	CH_COUNT,
};

static const struct gpio_dt_spec channels[CH_COUNT] = {
	[CH_RED] = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	[CH_GREEN] = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
	[CH_BLUE] = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
};

/* Colours as a channel bitmask. */
#define COLOR_OFF	0
#define COLOR_RED	BIT(CH_RED)
#define COLOR_GREEN	BIT(CH_GREEN)
#define COLOR_BLUE	BIT(CH_BLUE)
#define COLOR_CYAN	(BIT(CH_GREEN) | BIT(CH_BLUE))
#define COLOR_MAGENTA	(BIT(CH_RED) | BIT(CH_BLUE))
#define COLOR_YELLOW	(BIT(CH_RED) | BIT(CH_GREEN))
#define COLOR_WHITE	(BIT(CH_RED) | BIT(CH_GREEN) | BIT(CH_BLUE))

/* Sub-states mirroring the four discrete LEDs, so both backends consume the
 * same events with the same meaning. Only the output stage differs: here
 * render() collapses them onto the one LED by priority.
 */
enum gnss_state {
	GNSS_IDLE,
	GNSS_SEARCHING,
	GNSS_FIX,
};

enum pass_state {
	PASS_IDLE,
	PASS_WINDOW,
	PASS_RRC,
	PASS_UDP_OK,	/* Latched until the next cycle starts. */
	PASS_SEND_FAILED,	/* Latched until the next cycle starts. */
};

/* Boot and TN, so the LED is not dark from power-on until the first GNSS search. */
enum boot_state {
	BOOT_NONE,
	BOOT_STARTING,
	BOOT_TN_CONNECTING,
	BOOT_TN_UP,
};

enum sched_state {
	SCHED_IDLE,
	SCHED_OK,
	SCHED_OK_FALLBACK,	/* Scheduled from the default position, not a fix. */
	SCHED_NONE,
};

static enum gnss_state gnss_state;
static enum pass_state pass_state;
static enum sched_state sched_state;
static enum boot_state boot_state = BOOT_STARTING;
static bool fatal_error;

/* What is currently on the LED. An on time of 0 means solid; otherwise the LED is on for
 * on_ms and off for off_ms.
 */
static uint8_t color;
static uint32_t on_ms;
static uint32_t off_ms;
static bool level;

static void blink_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(blink_work, blink_work_fn);

static void channels_apply(uint8_t mask)
{
	for (int i = 0; i < CH_COUNT; i++) {
		(void)gpio_pin_set_dt(&channels[i], (mask & BIT(i)) ? 1 : 0);
	}
}

static void blink_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	level = !level;
	channels_apply(level ? color : COLOR_OFF);
	(void)k_work_schedule(&blink_work, K_MSEC(level ? on_ms : off_ms));
}

/* Picks the colour and blink rate of the highest-priority active sub-state.
 *
 * GNSS_SEARCHING deliberately outranks the schedule so the search blink stays
 * visible even with a pass already scheduled, while the steady GNSS_FIX ranks
 * below it so the scheduled-pass cyan owns the long idle period between passes.
 */
static void render(void)
{
	uint8_t new_color;
	uint32_t new_period;
	uint32_t new_on;
	uint32_t new_off;

	if (fatal_error) {
		new_color = COLOR_RED;
		new_period = 0;
	} else if (pass_state == PASS_UDP_OK) {
		new_color = COLOR_GREEN;
		new_period = 0;
	} else if (pass_state == PASS_SEND_FAILED) {
		new_color = COLOR_RED;
		new_period = BLINK_SLOW_MS;
	} else if (pass_state == PASS_RRC) {
		new_color = COLOR_GREEN;
		new_period = BLINK_FAST_MS;
	} else if (pass_state == PASS_WINDOW) {
		new_color = COLOR_GREEN;
		new_period = BLINK_NORMAL_MS;
	} else if (boot_state == BOOT_STARTING) {
		new_color = COLOR_MAGENTA;
		new_period = BLINK_FAST_MS;
	} else if (boot_state == BOOT_TN_CONNECTING) {
		new_color = COLOR_MAGENTA;
		new_period = BLINK_NORMAL_MS;
	} else if (boot_state == BOOT_TN_UP) {
		new_color = COLOR_MAGENTA;
		new_period = 0;
	} else if (gnss_state == GNSS_SEARCHING) {
		new_color = COLOR_BLUE;
		new_period = BLINK_NORMAL_MS;
	} else if (sched_state == SCHED_OK) {
		new_color = COLOR_CYAN;
		new_period = 0;
	} else if (sched_state == SCHED_OK_FALLBACK) {
		new_color = COLOR_YELLOW;
		new_period = 0;
	} else if (sched_state == SCHED_NONE) {
		new_color = COLOR_CYAN;
		new_period = BLINK_SLOW_MS;
	} else if (gnss_state == GNSS_FIX) {
		new_color = COLOR_BLUE;
		new_period = 0;
	} else {
		new_color = COLOR_WHITE;
		new_period = 0;
		new_on = HEARTBEAT_ON_MS;
		new_off = HEARTBEAT_PERIOD_MS - HEARTBEAT_ON_MS;
		goto apply;
	}

	/* Every other state is solid (period 0) or blinks with equal on and off times. */
	new_on = new_period;
	new_off = new_period;

apply:
	if (new_color == color && new_on == on_ms && new_off == off_ms) {
		return;
	}

	color = new_color;
	on_ms = new_on;
	off_ms = new_off;

	(void)k_work_cancel_delayable(&blink_work);

	level = true;
	channels_apply(color);

	if (on_ms) {
		(void)k_work_schedule(&blink_work, K_MSEC(on_ms));
	}
}

/* Drops the latched result of the previous pass. Called on every event that
 * marks the start of a new cycle, so a solid green uplink-ok does not mask the
 * GNSS search or the next schedule.
 */
static void pass_cycle_reset(void)
{
	pass_state = PASS_IDLE;
}

void ntn_led_tn_connecting(void)
{
	boot_state = BOOT_TN_CONNECTING;
	render();
}

void ntn_led_tn_up(void)
{
	boot_state = BOOT_TN_UP;
	render();
}

void ntn_led_boot_done(void)
{
	boot_state = BOOT_NONE;
	render();
}

void ntn_led_gnss_searching(void)
{
	boot_state = BOOT_NONE;
	pass_cycle_reset();

	gnss_state = GNSS_SEARCHING;
	render();
}

void ntn_led_gnss_fix(void)
{
	gnss_state = GNSS_FIX;
	render();
}

void ntn_led_gnss_timeout(void)
{
	gnss_state = GNSS_IDLE;
	render();
}

void ntn_led_pass_scheduled(void)
{
	pass_cycle_reset();

	sched_state = SCHED_OK;
	render();
}

void ntn_led_pass_scheduled_fallback(void)
{
	pass_cycle_reset();

	sched_state = SCHED_OK_FALLBACK;
	render();
}

void ntn_led_no_pass(void)
{
	pass_cycle_reset();

	sched_state = SCHED_NONE;
	render();
}

void ntn_led_pass_start(void)
{
	sched_state = SCHED_IDLE;
	pass_state = PASS_WINDOW;
	render();
}

void ntn_led_pass_progress(void)
{
	if (pass_state != PASS_UDP_OK && pass_state != PASS_SEND_FAILED) {
		pass_state = PASS_RRC;
		render();
	}
}

void ntn_led_udp_ok(void)
{
	pass_state = PASS_UDP_OK;
	render();
}

void ntn_led_send_failed(void)
{
	if (pass_state != PASS_UDP_OK) {
		pass_state = PASS_SEND_FAILED;
		render();
	}
}

void ntn_led_pass_end(void)
{
	if (pass_state != PASS_UDP_OK && pass_state != PASS_SEND_FAILED) {
		pass_state = PASS_IDLE;
		render();
	}
}

void ntn_led_fatal_error(void)
{
	fatal_error = true;
	render();
}

static int ntn_led_init(void)
{
	int err;

	for (int i = 0; i < CH_COUNT; i++) {
		if (!gpio_is_ready_dt(&channels[i])) {
			LOG_ERR("RGB channel %d GPIO not ready", i);

			return -ENODEV;
		}

		err = gpio_pin_configure_dt(&channels[i], GPIO_OUTPUT_INACTIVE);
		if (err) {
			LOG_ERR("Failed to configure RGB channel %d, error: %d", i, err);

			return err;
		}
	}

	/* Show the boot indication right away. */
	render();

	return 0;
}

SYS_INIT(ntn_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
