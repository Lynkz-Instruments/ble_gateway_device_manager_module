/**
 * @file ble_gw_dm_device_id_init.c
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr.h>
#include <init.h>
#include <stdio.h>
#include "attr.h"

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static int ble_gw_dm_device_id_init(const struct device *device);

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
SYS_INIT(ble_gw_dm_device_id_init, APPLICATION, CONFIG_LCZ_BLE_GW_DM_DEVICE_ID_INIT_PRIORITY);

/**************************************************************************************************/
/* SYS INIT                                                                                       */
/**************************************************************************************************/
static int ble_gw_dm_device_id_init(const struct device *device)
{
	ARG_UNUSED(device);

	char id[sizeof(uint64_t) * 2 + 1];
	char *dev_id;
	uint32_t dev_id_0;
	uint32_t dev_id_1;

#if defined(CONFIG_BOARD_MG100) || defined(CONFIG_BOARD_PINNACLE_100_DVK)
	dev_id_0 = NRF_FICR->DEVICEID[0];
	dev_id_1 = NRF_FICR->DEVICEID[1];
#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
	dev_id_0 = NRF_FICR->INFO.DEVICEID[0];
	dev_id_1 = NRF_FICR->INFO.DEVICEID[1];
#else
#error "Unsupported board"
#endif

	dev_id = (char *)attr_get_quasi_static(ATTR_ID_device_id);

	if (strlen(dev_id) < 1) {
		(void)snprintf(id, sizeof(id), "%08x%08x", dev_id_1, dev_id_0);
		attr_set_string(ATTR_ID_device_id, id, strlen(id));
	}

	return 0;
}
