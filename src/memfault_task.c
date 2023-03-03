/**
 * @file memfault_task.c
 * @brief
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(memfault_task, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr/zephyr.h>
#include <zephyr/drivers/modem/hl7800.h>
#include <memfault_ncs.h>
#if defined(CONFIG_ATTR)
#include <attr.h>
#endif
#include <lcz_memfault.h>
#include <file_system_utilities.h>

#include "memfault_task.h"

/**************************************************************************************************/
/* Local Constant, Macro and Type Definitions                                                     */
/**************************************************************************************************/
#define MEMFAULT_DATA_FILE_PATH CONFIG_FSU_MOUNT_POINT "/" CONFIG_LCZ_BLE_GW_DM_MEMFAULT_FILE_NAME
#define SEND_SYNC_TIMEOUT_MINUTES 10

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
static struct k_timer report_data_timer;
static uint8_t chunk_buf[CONFIG_LCZ_BLE_GW_DM_MEMFAULT_CHUNK_BUF_SIZE];
/* Semaphore for API thread safety */
static K_SEM_DEFINE(send_lock_sem, 1, 1);
/* Semaphore for data sent sync */
static K_SEM_DEFINE(send_wait_sem, 0, 1);

/**************************************************************************************************/
/* Global Data Definitions                                                                        */
/**************************************************************************************************/
extern const k_tid_t memfault;

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static void report_data_timer_expired(struct k_timer *timer_id);
static bool save_data(void);
static char *get_mflt_transport_str(enum memfault_transport type);

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
static void report_data_timer_expired(struct k_timer *timer_id)
{
	(void)lcz_ble_gw_dm_memfault_post_data();
}

static int post_or_publish(void)
{
	/* Always use HTTP for the first report. */
	static enum memfault_transport mflt_transport = MEMFAULT_TRANSPORT_HTTP;
	int ret = 0;

	uint32_t transport_attr = attr_get_uint32(ATTR_ID_memfault_transport, 0);

	if(transport_attr != MEMFAULT_TRANSPORT_NONE) {
		LOG_DBG("Posting Memfault data...");

		if (mflt_transport == MEMFAULT_TRANSPORT_MQTT) {
			ret = LCZ_MEMFAULT_PUBLISH_DATA(chunk_buf, sizeof(chunk_buf), K_FOREVER);
		} else if (mflt_transport == MEMFAULT_TRANSPORT_COAP) {
			ret = LCZ_MEMFAULT_COAP_PUBLISH_DATA(chunk_buf, sizeof(chunk_buf), K_FOREVER);
		} else {
			ret = LCZ_MEMFAULT_POST_DATA_V2(chunk_buf, sizeof(chunk_buf));
		}

		LOG_DBG("Memfault data sent (%s): %d", get_mflt_transport_str(mflt_transport), ret);
	} else {
		LOG_DBG("Memfault publish disabled (%s)", get_mflt_transport_str(mflt_transport));
	}

	if (LCZ_MEMFAULT_MQTT_ENABLED()) {
		mflt_transport = MEMFAULT_TRANSPORT_MQTT;
	} else if(LCZ_MEMFAULT_COAP_ENABLED()) {
		mflt_transport = MEMFAULT_TRANSPORT_COAP;
	} else {
		mflt_transport = MEMFAULT_TRANSPORT_HTTP;
	}
	
	return ret;
}

static bool save_data(void)
{
#ifdef CONFIG_MODEM_HL7800
#ifdef CONFIG_ATTR
#if defined(ATTR_ID_store_memfault_data)
	if(attr_get_uint32(ATTR_ID_store_memfault_data, 0) == 1) {
		return true;
	} else {
		return false;
	}
#endif
	if (attr_get_uint32(ATTR_ID_lte_rat, 0) == MDM_RAT_CAT_NB1) {
		return true;
	} else
#endif
	{
		return false;
	}
#else
	return false;
#endif
}

static void memfault_thread(void *arg1, void *arg2, void *arg3)
{
	char *dev_id;
	size_t file_size;
	bool has_coredump;
	bool delete_file;
	int ret;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	dev_id = (char *)attr_get_quasi_static(ATTR_ID_device_id);

	memfault_ncs_device_id_set(dev_id, strlen(dev_id));

	LCZ_MEMFAULT_HTTP_INIT();
	k_timer_init(&report_data_timer, report_data_timer_expired, NULL);

	k_timer_start(&report_data_timer,
		      K_SECONDS(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_REPORT_PERIOD_SECONDS),
		      K_SECONDS(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_REPORT_PERIOD_SECONDS));

	while (true) {
		k_thread_suspend(memfault);

		if (save_data()) {
			LOG_DBG("Saving Memfault data...");
			if (fsu_get_file_size_abs(MEMFAULT_DATA_FILE_PATH) >=
			    CONFIG_LCZ_BLE_GW_DM_MEMFAULT_FILE_MAX_SIZE_BYTES) {
				delete_file = true;
			} else {
				delete_file = false;
			}
			ret = lcz_memfault_save_data_to_file(MEMFAULT_DATA_FILE_PATH, chunk_buf,
							     sizeof(chunk_buf), delete_file, true,
							     &file_size, &has_coredump);
			if (ret == 0) {
				LOG_DBG("Memfault data saved!");
			}
		} else {
			post_or_publish();
		}

		k_sem_give(&send_wait_sem);

		/* Reset timer each time data is sent */
		k_timer_start(&report_data_timer,
			      K_SECONDS(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_REPORT_PERIOD_SECONDS),
			      K_SECONDS(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_REPORT_PERIOD_SECONDS));
	}
}

static char * get_mflt_transport_str(enum memfault_transport type)
{
    switch(type) {
        case MEMFAULT_TRANSPORT_NONE:
		return "NONE";
        case MEMFAULT_TRANSPORT_HTTP:
		return "HTTP";
	    case MEMFAULT_TRANSPORT_MQTT:
		return "MQTT";
        case MEMFAULT_TRANSPORT_COAP:
		return "COAP";
        default:
		return "";
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

int lcz_ble_gw_dm_memfault_post_data_sync(void)
{
	int ret;

	k_sem_take(&send_lock_sem, K_FOREVER);
	k_sem_reset(&send_wait_sem);
	k_thread_resume(memfault);
	ret = k_sem_take(&send_wait_sem, K_MINUTES(SEND_SYNC_TIMEOUT_MINUTES));
	k_sem_give(&send_lock_sem);
	return ret;
}

K_THREAD_DEFINE(memfault, CONFIG_LCZ_BLE_GW_DM_MEMFAULT_THREAD_STACK_SIZE, memfault_thread, NULL,
		NULL, NULL, K_PRIO_PREEMPT(CONFIG_LCZ_BLE_GW_DM_MEMFAULT_THREAD_PRIORITY), 0, 0);
