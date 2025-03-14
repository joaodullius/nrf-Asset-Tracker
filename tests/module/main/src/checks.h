/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "location.h"
#include "network.h"
#include "power.h"

void check_location_event(enum location_msg_type expected_location_type);

void check_network_event(enum network_msg_type expected_network_type);

void check_power_event(enum power_msg_type expected_power_type);

void check_no_events(uint32_t timeout_sec);

void purge_location_events(void);

void purge_network_events(void);

void purge_power_events(void);

void purge_all_events(void);
