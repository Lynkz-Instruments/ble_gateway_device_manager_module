/**
 * @file memfault_task.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(memfault_task, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr.h>
#include <drivers/modem/hl7800.h>
#include <memfault_ncs.h>
#include "lcz_memfault.h"
#include "memfault_task.h"

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
struct k_timer report_data_timer;

/**************************************************************************************************/
/* Global Data Definitions                                                                        */
/**************************************************************************************************/
extern const k_tid_t memfault;

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static void report_data_timer_expired(struct k_timer *timer_id);

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
static void report_data_timer_expired(struct k_timer *timer_id)
{
	(void)lcz_ble_gw_dm_memfault_post_data();
}

static void memfault_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

#ifdef CONFIG_MODEM_HL7800
	char *imei = mdm_hl7800_get_imei();
	memfault_ncs_device_id_set(imei, strlen(imei));
#endif

	LCZ_MEMFAULT_HTTP_INIT();
	k_timer_init(&report_data_timer, report_data_timer_expired, NULL);

	k_timer_start(&report_data_timer,
		      K_SECONDS(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_REPORT_PERIOD_SECONDS),
		      K_SECONDS(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_REPORT_PERIOD_SECONDS));

	while (true) {
		k_thread_suspend(memfault);
		LOG_INF("Posting Memfault data...");
		LCZ_MEMFAULT_POST_DATA();
		LOG_INF("Memfault data sent!");
		/* Reset timer each time data is sent */
		k_timer_start(&report_data_timer,
			      K_SECONDS(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_REPORT_PERIOD_SECONDS),
			      K_SECONDS(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_REPORT_PERIOD_SECONDS));
	}
}

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
int lcz_ble_gw_dm_memfault_post_data(void)
{
	k_thread_resume(memfault);
	return 0;
}

K_THREAD_DEFINE(memfault, CONFIG_LCZ_BLE_GW_DM_MEMFAULT_THREAD_STACK_SIZE, memfault_thread, NULL,
		NULL, NULL, K_PRIO_PREEMPT(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_THREAD_PRIORITY), 0, 0);
