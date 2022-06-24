/**
 * @file hl7800.c
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(ble_gw_dm_hl7800, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr.h>
#include <init.h>
#include <zephyr/drivers/modem/hl7800.h>
#if defined(CONFIG_ATTR)
#include "attr.h"
#endif

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
static struct mdm_hl7800_callback_agent hl7800_evt_agent;

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static int hl7800_init(const struct device *device);
static void hl7800_event_callback(enum mdm_hl7800_event event, void *event_data);

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
static void hl7800_event_callback(enum mdm_hl7800_event event, void *event_data)
{
	switch (event) {
	case HL7800_EVENT_RAT:
#if defined(CONFIG_ATTR)
		attr_set_uint32(ATTR_ID_lte_rat, *((uint8_t *)event_data));
#endif
		break;
	default:
		break;
	}
}

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/

SYS_INIT(hl7800_init, APPLICATION, CONFIG_LCZ_BLE_GW_DM_HL7800_INIT_PRIORITY);

/**************************************************************************************************/
/* SYS INIT                                                                                       */
/**************************************************************************************************/
static int hl7800_init(const struct device *device)
{
	ARG_UNUSED(device);

	hl7800_evt_agent.event_callback = hl7800_event_callback;
	mdm_hl7800_register_event_callback(&hl7800_evt_agent);
	mdm_hl7800_generate_status_events();

	return 0;
}
