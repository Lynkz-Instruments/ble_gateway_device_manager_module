/**
 * @file ble_gw_dm_ble.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(ble_gw_dm_ble, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr/bluetooth/bluetooth.h>
#include "attr.h"
#include "ble_gw_dm_ble.h"

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
int ble_gw_dm_device_ble_addr_init(void)
{
	int ret = 0;
	size_t count = 1;
	bt_addr_le_t addr;
	char addr_str[BT_ADDR_LE_STR_LEN] = { 0 };
	char bd_addr[BT_ADDR_LE_STR_LEN];
	size_t size;

#if defined(CONFIG_ATTR)
	size = attr_get_size(ATTR_ID_bluetooth_address);
#else
	size = BT_ADDR_LE_STR_LEN;
#endif

	(void)bt_enable(NULL);

	bt_id_get(&addr, &count);
	if (count < 1) {
		LOG_DBG("Creating new address");
		bt_addr_le_copy(&addr, BT_ADDR_LE_ANY);
		ret = bt_id_create(&addr, NULL);
	}
	bt_addr_le_to_str(&addr, addr_str, sizeof(addr_str));
	LOG_INF("Bluetooth Address: %s count: %d status: %d", addr_str, count, ret);

	/* remove ':' from default format */
	size_t i;
	size_t j;
	for (i = 0, j = 0; j < size - 1; i++) {
		if (addr_str[i] != ':') {
			bd_addr[j] = addr_str[i];
			if (bd_addr[j] >= 'A' && bd_addr[j] <= 'Z') {
				bd_addr[j] += ('a' - 'A');
			}
			j += 1;
		}
	}
	bd_addr[j] = 0;
#if defined(CONFIG_ATTR)
	attr_set_string(ATTR_ID_bluetooth_address, bd_addr, size - 1);
#endif

	return ret;
}
