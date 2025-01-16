/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <unity.h>

#include <zephyr/fff.h>
#include "message_channel.h"
#include <zephyr/task_wdt/task_wdt.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, task_wdt_feed, int);
FAKE_VALUE_FUNC(int, task_wdt_add, uint32_t, task_wdt_callback_t, void *);
FAKE_VALUE_FUNC(int, nrf_cloud_client_id_get, char *, size_t);
FAKE_VALUE_FUNC(int, nrf_cloud_coap_init);
FAKE_VALUE_FUNC(int, nrf_cloud_coap_connect, const char * const);
FAKE_VALUE_FUNC(int, nrf_cloud_coap_disconnect);
FAKE_VALUE_FUNC(int, nrf_cloud_coap_shadow_device_status_update);
FAKE_VALUE_FUNC(int, nrf_cloud_coap_bytes_send, uint8_t *, size_t, bool);
FAKE_VALUE_FUNC(int, nrf_cloud_coap_sensor_send, const char *, double, int64_t, bool);
FAKE_VALUE_FUNC(int, nrf_cloud_coap_json_message_send, const char *, bool, bool);

/* Forward declarations */
static void dummy_cb(const struct zbus_channel *chan);
static void cloud_chan_cb(const struct zbus_channel *chan);
static void error_cb(const struct zbus_channel *chan);

/* Define unused subscribers */
ZBUS_SUBSCRIBER_DEFINE(app, 1);
ZBUS_SUBSCRIBER_DEFINE(battery, 1);
ZBUS_SUBSCRIBER_DEFINE(environmental, 1);
ZBUS_SUBSCRIBER_DEFINE(fota, 1);
ZBUS_SUBSCRIBER_DEFINE(led, 1);
ZBUS_SUBSCRIBER_DEFINE(location, 1);
ZBUS_LISTENER_DEFINE(trigger, dummy_cb);
ZBUS_LISTENER_DEFINE(cloud, cloud_chan_cb);
ZBUS_LISTENER_DEFINE(error, error_cb);

#define FAKE_DEVICE_ID		"test_device"

static K_SEM_DEFINE(cloud_disconnected, 0, 1);
static K_SEM_DEFINE(cloud_connected_ready, 0, 1);
static K_SEM_DEFINE(cloud_connected_paused, 0, 1);
static K_SEM_DEFINE(data_sent, 0, 1);
static K_SEM_DEFINE(fatal_error_received, 0, 1);

static int nrf_cloud_client_id_get_custom_fake(char *buf, size_t len)
{
	TEST_ASSERT(len >= sizeof(FAKE_DEVICE_ID));
	memcpy(buf, FAKE_DEVICE_ID, sizeof(FAKE_DEVICE_ID));

	return 0;
}

static void dummy_cb(const struct zbus_channel *chan)
{
	ARG_UNUSED(chan);
}

static void cloud_chan_cb(const struct zbus_channel *chan)
{
	if (chan == &CLOUD_CHAN) {
		enum cloud_msg_type status = *(enum cloud_msg_type *)chan->message;

		if (status == CLOUD_DISCONNECTED) {
			k_sem_give(&cloud_disconnected);
		} else if (status == CLOUD_CONNECTED_READY_TO_SEND) {
			k_sem_give(&cloud_connected_ready);
		} else if (status == CLOUD_CONNECTED_PAUSED) {
			k_sem_give(&cloud_connected_paused);
		}
	}
}

static void error_cb(const struct zbus_channel *chan)
{
	if (chan == &ERROR_CHAN) {
		enum error_type type = *(enum error_type *)chan->message;

		if (type == ERROR_FATAL) {
			k_sem_give(&fatal_error_received);
		}
	}
}

void setUp(void)
{
	const struct zbus_channel *chan;

	/* Reset fakes */
	RESET_FAKE(task_wdt_feed);
	RESET_FAKE(task_wdt_add);
	RESET_FAKE(nrf_cloud_client_id_get);
	RESET_FAKE(nrf_cloud_coap_json_message_send);
	RESET_FAKE(nrf_cloud_coap_connect);

	nrf_cloud_client_id_get_fake.custom_fake = nrf_cloud_client_id_get_custom_fake;

	/* Clear all channels */
	zbus_sub_wait(&location, &chan, K_NO_WAIT);
	zbus_sub_wait(&app, &chan, K_NO_WAIT);
	zbus_sub_wait(&fota, &chan, K_NO_WAIT);
	zbus_sub_wait(&led, &chan, K_NO_WAIT);
	zbus_sub_wait(&battery, &chan, K_NO_WAIT);

	zbus_chan_add_obs(&CLOUD_CHAN, &cloud, K_NO_WAIT);
	zbus_chan_add_obs(&ERROR_CHAN, &error, K_NO_WAIT);
}

void test_initial_transition_to_disconnected(void)
{
	int err;

	err = k_sem_take(&cloud_disconnected, K_SECONDS(1));
	TEST_ASSERT_EQUAL(0, err);
}

void test_connecting_backoff(void)
{
	int err;
	struct network_msg msg = { .type = NETWORK_CONNECTED, };
	uint64_t connect_start_time, connect_duration_sec;

	nrf_cloud_coap_connect_fake.return_val = -EAGAIN;
	connect_start_time = k_uptime_get();

	msg.type = NETWORK_CONNECTED;

	err = zbus_chan_pub(&NETWORK_CHAN, &msg, K_SECONDS(1));
	TEST_ASSERT_EQUAL(0, err);

	/* Transport module needs CPU to run state machine */
	k_sleep(K_MSEC(10));

	err = k_sem_take(&cloud_connected_ready, K_SECONDS(60));
	TEST_ASSERT_EQUAL(-EAGAIN, err);

	connect_duration_sec = k_uptime_delta(&connect_start_time) / MSEC_PER_SEC;

	/* Check that the connection attempt took at least
	 * CONFIG_APP_TRANSPORT_BACKOFF_INITIAL_SECONDS
	 */

	TEST_ASSERT_GREATER_OR_EQUAL(CONFIG_APP_TRANSPORT_BACKOFF_INITIAL_SECONDS,
				     connect_duration_sec);
}

void test_transition_disconnected_connected_ready(void)
{
	int err;
	enum network_msg_type status = NETWORK_CONNECTED;

	zbus_chan_pub(&NETWORK_CHAN, &status, K_NO_WAIT);

	err = k_sem_take(&cloud_connected_ready, K_SECONDS(1));
	TEST_ASSERT_EQUAL(0, err);
}

void test_sending_payload(void)
{
	int err;
	struct payload payload = {
		.buffer = "{\"test\": 1}",
		.buffer_len = strlen(payload.buffer),
	};

	err = zbus_chan_pub(&PAYLOAD_CHAN, &payload, K_SECONDS(1));
	TEST_ASSERT_EQUAL(0, err);

	/* Transport module needs CPU to run state machine */
	k_sleep(K_MSEC(10));

	TEST_ASSERT_EQUAL(1, nrf_cloud_coap_json_message_send_fake.call_count);
	TEST_ASSERT_EQUAL(0, strncmp(nrf_cloud_coap_json_message_send_fake.arg0_val,
				     payload.buffer, payload.buffer_len));
	TEST_ASSERT_EQUAL(false, nrf_cloud_coap_json_message_send_fake.arg1_val);
	TEST_ASSERT_EQUAL(false, nrf_cloud_coap_json_message_send_fake.arg2_val);
}

void test_connected_ready_to_paused(void)
{
	int err;
	enum network_msg_type status = NETWORK_DISCONNECTED;

	zbus_chan_pub(&NETWORK_CHAN, &status, K_NO_WAIT);

	/* Transport module needs CPU to run state machine */
	k_sleep(K_MSEC(100));

	err = k_sem_take(&cloud_connected_paused, K_SECONDS(1));
	TEST_ASSERT_EQUAL(0, err);
}

void test_connected_paused_to_ready_send_payload(void)
{
	int err;
	enum network_msg_type status = NETWORK_CONNECTED;
	struct payload payload = {
		.buffer = "{\"Another\": \"test\"}",
		.buffer_len = strlen(payload.buffer),
	};

	/* Reset call count */
	nrf_cloud_coap_bytes_send_fake.call_count = 0;

	err = zbus_chan_pub(&NETWORK_CHAN, &status, K_NO_WAIT);
	TEST_ASSERT_EQUAL(0, err);

	/* Transport module needs CPU to run state machine */
	k_sleep(K_MSEC(10));

	err = k_sem_take(&cloud_connected_ready, K_SECONDS(1));
	TEST_ASSERT_EQUAL(0, err);

	err = zbus_chan_pub(&PAYLOAD_CHAN, &payload, K_NO_WAIT);
	TEST_ASSERT_EQUAL(0, err);

	/* Transport module needs CPU to run state machine */
	k_sleep(K_MSEC(10));

	TEST_ASSERT_EQUAL(1, nrf_cloud_coap_json_message_send_fake.call_count);
	TEST_ASSERT_EQUAL(0, strncmp(nrf_cloud_coap_json_message_send_fake.arg0_val,
				     payload.buffer, payload.buffer_len));
	TEST_ASSERT_EQUAL(false, nrf_cloud_coap_json_message_send_fake.arg1_val);
	TEST_ASSERT_EQUAL(false, nrf_cloud_coap_json_message_send_fake.arg2_val);
}

/* This is required to be added to each test. That is because unity's
 * main may return nonzero, while zephyr's main currently must
 * return 0 in all cases (other values are reserved).
 */
extern int unity_main(void);

int main(void)
{
	/* use the runner from test_runner_generate() */
	(void)unity_main();

	return 0;
}
