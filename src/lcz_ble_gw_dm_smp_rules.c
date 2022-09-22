/**
 * @file lcz_ble_gw_dm_dmp_rules.c
 * @brief Holds rules for SMP access for the DM gateway
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lcz_ble_gw_dm_smp_rules, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr.h>
#include <init.h>
#include <mgmt/mgmt.h>
#if defined(CONFIG_BT_PERIPHERAL)
#include <bluetooth/conn.h>
#endif

#if defined(CONFIG_LCZ_PKI_AUTH_SMP_PERIPHERAL)
#include "lcz_pki_auth_smp.h"
#endif

#if defined(CONFIG_ATTR)
#include "attr.h"
#endif

/**************************************************************************************************/
/* Local Constant, Macro and Type Definitions                                                     */
/**************************************************************************************************/

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
#if defined(CONFIG_BT_PERIPHERAL)
static void bt_disconnected(struct bt_conn *conn, uint8_t reason);
#endif
#if defined(CONFIG_LCZ_PKI_AUTH_SMP_PERIPHERAL)
static void auth_complete_cb(bool status);
static void reset_auth_timer(void);
static void smp_auth_timeout_work_handler(struct k_work *work);
#endif
static bool gw_dm_smp_test(uint16_t group_id, uint16_t command_id);
static int lcz_ble_gw_dm_smp_rules_init(const struct device *device);

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
#if defined(CONFIG_LCZ_PKI_AUTH_SMP_PERIPHERAL)
struct lcz_pki_auth_smp_periph_auth_callback_agent auth_cb = { .cb = auth_complete_cb };
static K_WORK_DELAYABLE_DEFINE(smp_auth_timeout_work, smp_auth_timeout_work_handler);
#endif
#if defined(CONFIG_BT_PERIPHERAL)
static struct bt_conn_cb conn_callbacks = { .disconnected = bt_disconnected };
#endif
#if defined(CONFIG_BT_PERIPHERAL) || defined(CONFIG_LCZ_PKI_AUTH_SMP_PERIPHERAL)
static bool authorized = false;
#endif

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
#if defined(CONFIG_BT_PERIPHERAL)
static void bt_disconnected(struct bt_conn *conn, uint8_t reason)
{
    /* As blanket protection, set state to unauthorized after every BLE connection ends */
    authorized = false;
}
#endif

#if defined(CONFIG_LCZ_PKI_AUTH_SMP_PERIPHERAL)
static void auth_complete_cb(bool status)
{
	authorized = status;
	if (authorized) {
		reset_auth_timer();
	}
}

static void reset_auth_timer(void)
{
	k_timeout_t auth_timeout = K_SECONDS(CONFIG_LCZ_GW_DM_SMP_AUTH_TIMEOUT);

#if defined(ATTR_ID_smp_auth_timeout)
	auth_timeout = K_SECONDS(
		attr_get_uint32(ATTR_ID_smp_auth_timeout, CONFIG_LCZ_GW_DM_SMP_AUTH_TIMEOUT));
#endif

	k_work_reschedule(&smp_auth_timeout_work, auth_timeout);
}

static void smp_auth_timeout_work_handler(struct k_work *work)
{
    authorized = false;
}
#endif

static bool gw_dm_smp_test(uint16_t group_id, uint16_t command_id)
{
#if !defined(CONFIG_LCZ_PKI_AUTH_SMP_PERIPHERAL)
	/* If we don't support peripheral authentication, everything is always allowed */
	return true;
#else
	/* Always allow the authentication group */
	if (group_id == CONFIG_LCZ_PKI_AUTH_SMP_GROUP_ID) {
		return true;
	}

#if defined(ATTR_ID_smp_auth_req)
	/* If the attribute says authentication isn't required, allow anything */
	if (*(bool *)attr_get_quasi_static(ATTR_ID_smp_auth_req) == false) {
		return true;
	}

	/* If we were recently authorized, allow anything */
	if (authorized && k_work_delayable_is_pending(&smp_auth_timeout_work)) {
		reset_auth_timer();
		return true;
	}

	/* Not authorized. Reject everything else. */
	authorized = false;
	return false;
#else
	/* If the attribute doesn't exist, assume that everything is allowed */
	return true;
#endif /* defined(ATTR_ID_smp_auth_req) */
#endif /* !defined(CONFIG_LCZ_PKI_AUTH_SMP_PERIPHERAL) */
}

/**************************************************************************************************/
/* SYS INIT                                                                                       */
/**************************************************************************************************/
SYS_INIT(lcz_ble_gw_dm_smp_rules_init, APPLICATION, CONFIG_LCZ_GW_DM_SMP_RULES_INIT_PRIORITY);
static int lcz_ble_gw_dm_smp_rules_init(const struct device *device)
{
	/* Register our rules function with the mgmt layer */
	mgmt_register_permission_cb(gw_dm_smp_test);

#if defined(CONFIG_LCZ_PKI_AUTH_SMP_PERIPHERAL)
	/* Register our callback with the SMP authorization client */
	lcz_pki_auth_smp_periph_register_handler(&auth_cb);
#endif

#if defined(CONFIG_BT_PERIPHERAL)
	/* Register for BT callbacks */
	bt_conn_cb_register(&conn_callbacks);
#endif

	return 0;
}
