/**
 * @file ble_gw_dm_ble.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_gw_dm_ble, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr/init.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>
#include <attr.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include "ble_gw_dm_ble.h"

static struct k_work advertise_work;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		      0x84, 0xaa, 0x60, 0x74, 0x52, 0x8a, 0x8b, 0x86,
		      0xd3, 0x4c, 0xb7, 0x1d, 0x1d, 0xdc, 0x53, 0x8d),
};
static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME)
};

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static int ble_gw_dm_device_ble_addr_init(const struct device *device);
static void bt_ready(int err);
static void advertise(struct k_work *work);
static void connected(struct bt_conn *conn, uint8_t err);
static void disconnected(struct bt_conn *conn, uint8_t reason);

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
SYS_INIT(ble_gw_dm_device_ble_addr_init, APPLICATION, CONFIG_LCZ_BLE_GW_DM_BLE_ADDR_INIT_PRIORITY);

/**************************************************************************************************/
/* SYS INIT                                                                                       */
/**************************************************************************************************/
static int ble_gw_dm_device_ble_addr_init(const struct device *device)
{
	int ret = 0;
	size_t count = 1;
	bt_addr_le_t addr;
	char addr_str[BT_ADDR_LE_STR_LEN] = { 0 };
	char bd_addr[BT_ADDR_LE_STR_LEN];
	size_t size;
	int load_status;

	ARG_UNUSED(device);

#if defined(CONFIG_ATTR)
	size = attr_get_size(ATTR_ID_bluetooth_address);
#else
	size = BT_ADDR_LE_STR_LEN;
#endif
	//Register SMP
	bt_conn_cb_register(&conn_callbacks);
	k_work_init(&advertise_work, advertise);
	/* Enable Bluetooth. */
	(void)bt_enable(bt_ready);

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		load_status = settings_load();
		if (!load_status) {
			LOG_DBG("(bonding) settings load success");
		} else {
			LOG_ERR("(bonding) settings load failed %d", load_status);
		}
	}

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

static void advertise(struct k_work *work)
{
	int rc;

	rc = bt_le_adv_stop();
	if (rc) {
		LOG_WRN("Advertising failed to stop (rc %d)", rc);
	}

	rc = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (rc) {
		LOG_ERR("Advertising failed to start (rc %d)", rc);
		return;
	}

	LOG_INF("Advertising successfully started");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err 0x%02x)", err);
	} else {
		LOG_INF("Connected");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason 0x%02x)", reason);
	k_work_submit(&advertise_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	k_work_submit(&advertise_work);
}

