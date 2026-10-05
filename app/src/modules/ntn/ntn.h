/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NTN_H
#define NTN_H

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <nrf_modem_gnss.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NTN_TLE_NAME_MAX_LEN 30
#define NTN_TLE_LINE_MAX_LEN 80
#define NTN_SIB32_MAX_LEN 512
#define NTN_SIB31_MAX_LEN 512
#define NTN_SIB31_MAX_FIELD_COUNT 32
#define GNSS_GPS_PI 3.1415926535898
/* Saturate value x to the range [MIN_VALUE..MAX_VALUE] */
#define SATURATE(MIN_VALUE, x, MAX_VALUE) \
	((x) < (MIN_VALUE) ? (MIN_VALUE) : ((x) > (MAX_VALUE) ? (MAX_VALUE) : (x)))

/* NTN module message types */
enum ntn_msg_type {
	/* Events that trigger state transitions */
	NETWORK_CONNECTED,    /* Network connectivity established */
	NETWORK_DISCONNECTED, /* Network disconnected */
	NETWORK_CONNECTION_FAILED, /*  */
	NETWORK_CONNECTION_TIMEOUT, /*  */
	NTN_PDN_RESUMED, /* PDN connection resumed (context preserved) */
	NTN_RRC_CONNECTED, /* RRC connected */
	NTN_RRC_IDLE, /* RRC idle */
	NTN_CELL_FOUND, /* Cell found / modem reports searching or registered */
	NTN_NETWORK_REGISTERED, /* Modem registered on NTN network (CEREG=1 or 5) */
	LOCATION_SEARCH_DONE, /* Location search completed - triggers transition to NTN mode */
	LOCATION_REQUEST, /*  */
	GNSS_TIMEOUT, /* */
	NTN_TRIGGER,           /* LTE timer expired - connect to network */
	NTN_SHELL_SET_TIME,  /* Set new time of pass from shell */
	NTN_SHELL_SET_DATETIME, /* Set date time from shell */
	NTN_SHELL_SET_GNSS_LOCATION, /* Set GNSS location from shell */
	NTN_SHELL_SET_TLE, /* Set TLE from shell */
	NTN_SHELL_SET_PEAK_OFFSET, /* Set NTN activation offset from shell */
	NTN_SET_SIB32, /* Set SIB32 prediction data from shell or AT monitor */
	NTN_SET_SIB31, /* Set SIB31 prediction data from shell or AT monitor */
	NTN_CLEAR_SIB32, /* Clear cached SIB32 prediction data */
	KEEPALIVE_TIMER,     /* Keepalive timer */
	SGP4_TRIGGER, /* */
	GNSS_TRIGGER, /* */
	IDLE_TRIGGER, /* Force IDLE state from shell */
	/* Network acknowledged the last payload send (SO_SENDCB) */
	NTN_SEND_ACK,
	/* Payload send failed locally or was not acknowledged before timeout */
	NTN_SEND_FAILED,
	/* Force the TN state (nRF Cloud shadow TLE fetch) from shell */
	TN_TRIGGER,
	/* +CEREG reported an EMM reject cause while in NTN (reject_cause set) */
	NTN_ATTACH_REJECTED,
};

/* NTN module message */
struct ntn_msg {
	enum ntn_msg_type type;
	float sgp4_min_elevation_deg;
	char time_of_pass[32];
	char datetime[32];
	char tle_name[NTN_TLE_NAME_MAX_LEN];
	char tle_line1[NTN_TLE_LINE_MAX_LEN];
	char tle_line2[NTN_TLE_LINE_MAX_LEN];
	int32_t peak_offset_seconds;
	char sib32_data[NTN_SIB32_MAX_LEN];
	char sib31_data[NTN_SIB31_MAX_LEN];
	struct nrf_modem_gnss_pvt_data_frame pvt;
	/* EMM reject cause, for NTN_ATTACH_REJECTED */
	int reject_cause;
};

/* Attach phase of the two-step (two-pass) NTN attach, see APP_NTN_TWO_STEP_ATTACH */
enum ntn_attach_phase {
	NTN_ATTACH_PHASE_STEP1,		/* Next attach is step 1, expected to be rejected */
	NTN_ATTACH_PHASE_STEP2,		/* Step 1 rejected; step 2 runs on the next pass */
	NTN_ATTACH_PHASE_REGISTERED,	/* Attached; later passes resume the context */
};

/* Snapshot of the NTN attach bookkeeping, for the att_ntn attach_status command */
struct ntn_attach_status {
	bool two_step_enabled;
	enum ntn_attach_phase phase;
	int64_t phase_since_ms;		/* Uptime when the current phase started */
	uint32_t passes;		/* NTN state entries since boot */
	uint32_t step1_attempts;
	uint32_t step1_rejects;
	uint32_t step2_attempts;
	uint32_t step2_rejects;
	uint32_t registrations;
	int last_reject_cause;		/* -1 when no reject seen */
	int64_t last_reject_ms;		/* Uptime of the last reject, 0 if none */
	int64_t last_registered_ms;	/* Uptime of the last registration, 0 if none */
};

/* Copy the current attach bookkeeping. Safe to call from any thread. */
void ntn_attach_status_get(struct ntn_attach_status *out);

/* Return the two-step attach to step 1 and clear its counters. */
void ntn_attach_status_reset(void);

/* Name of an attach phase, for logs and the shell */
const char *ntn_attach_phase_str(enum ntn_attach_phase phase);

/* Declare the NTN message channel */
ZBUS_CHAN_DECLARE(NTN_CHAN);

#ifdef __cplusplus
}
#endif

#endif /* NTN_H */
