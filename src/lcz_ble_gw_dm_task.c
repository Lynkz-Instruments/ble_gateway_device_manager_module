/**
 * @file lcz_ble_gw_dm_task.c
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lcz_ble_gw_dm, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr.h>
#include <random/rand32.h>
#include <posix/time.h>
#include <date_time.h>
#if defined(CONFIG_MODEM_HL7800)
#include <zephyr/drivers/modem/hl7800.h>
#endif
#if defined(CONFIG_ATTR)
#include "attr.h"
#endif
#include "fwk_includes.h"
#include "lcz_ble_gw_dm_task.h"
#include "lcz_lwm2m_client.h"
#include "lcz_memfault.h"
#include "lcz_network_monitor.h"
#include "lcz_software_reset.h"
#include "lwm2m_telemetry.h"
#include "memfault_task.h"
#include "ble_gw_dm_ble.h"

/**************************************************************************************************/
/* Local Constant, Macro and Type Definitions                                                     */
/**************************************************************************************************/
#define GW_DM_TASK_QUEUE_DEPTH 8
#define DM_CONNECTION_DELAY_FALLBACK 5
#define RAND_RANGE(min, max) ((sys_rand32_get() % (max - min + 1)) + min)

#define CONNECTION_WATCHDOG_REBOOT_DELAY_MS 1000
#define CONNECTION_WATCHDOG_TIMEOUT_MULTIPLIER 2
#define CONNECTION_WATCHDOG_MAX_FALLBACK 300
#define CONNECTION_WATCHDOG_REBOOT_TIMER_TIMEOUT_MINUTES 60

enum gw_dm_state {
	GW_DM_STATE_WAIT_FOR_NETWORK = 0,
	GW_DM_STATE_GET_NETWORK_TIME,
	GW_DM_STATE_POST_MEMFAULT_DATA,
	GW_DM_STATE_WAIT_BEFORE_DM_CONNECTION,
	GW_DM_STATE_CONNECT_TO_DM,
	GW_DM_STATE_WAIT_FOR_CONNECTION,
#if defined(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M)
	GW_DM_STATE_CONNECT_TELEM,
	GW_DM_STATE_WAIT_FOR_TELEM_CONNECTION,
	GW_DM_STATE_DISCONNECT_TELEM,
#endif
	GW_DM_STATE_IDLE,
	GW_DM_STATE_DISCONNECT_DM,
};

typedef struct gw_dm_task_obj {
	FwkMsgTask_t msgTask;
	enum gw_dm_state state;
	uint32_t timer;
	bool network_ready;
	uint32_t dm_connection_delay_seconds;
	uint32_t dm_connection_timeout_seconds;
	bool lwm2m_connection_err;
	bool lwm2m_connected;
#if defined(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M)
	bool telem_enabled;
	bool lwm2m_telem_connected;
#endif
	uint32_t time;
} gw_dm_task_obj_t;

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static void nm_event_callback(enum lcz_nm_event event);
static FwkMsgHandler_t *gw_dm_task_msg_dispatcher(FwkMsgCode_t MsgCode);
static DispatchResult_t gateway_fsm_tick_handler(FwkMsgReceiver_t *pMsgRxer, FwkMsg_t *pMsg);
static void gw_dm_fsm(void);
static bool timer_expired(void);
static void set_state(enum gw_dm_state next_state);
static char *state_to_string(enum gw_dm_state state);
static void lwm2m_client_connected_event(struct lwm2m_ctx *client, int lwm2m_client_index,
					 bool connected, enum lwm2m_rd_client_event client_event);
static void connection_watchdog_timer_callback(struct k_timer *timer_id);
static void pet_connection_watchdog(bool in_connection);
static void *current_time_read_cb(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id,
				  size_t *data_len);
static void date_time_event_handler(const struct date_time_evt *evt);

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
static struct lcz_nm_event_agent event_agent;
static gw_dm_task_obj_t gwto;
K_MSGQ_DEFINE(gw_dm_task_queue, FWK_QUEUE_ENTRY_SIZE, GW_DM_TASK_QUEUE_DEPTH, FWK_QUEUE_ALIGNMENT);
static struct lcz_lwm2m_client_event_callback_agent lwm2m_event_agent;
static DispatchResult_t attr_broadcast_msg_handler(FwkMsgReceiver_t *pMsgRxer, FwkMsg_t *pMsg);
static void random_connect_handler(void);
static struct k_timer connection_watchdog_timer;
static struct k_timer connection_watchdog_reboot_timer;

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
static DispatchResult_t attr_broadcast_msg_handler(FwkMsgReceiver_t *pMsgRxer, FwkMsg_t *pMsg)
{
	int i;
	attr_changed_msg_t *pb = (attr_changed_msg_t *)pMsg;

	for (i = 0; i < pb->count; i++) {
		switch (pb->list[i]) {
		case ATTR_ID_dm_cnx_delay:
			random_connect_handler();
			break;

		default:
			/* Don't care about this attribute. This is a broadcast. */
			break;
		}
	}

	return DISPATCH_OK;
}

static void random_connect_handler(void)
{
	uint32_t delay = attr_get_uint32(ATTR_ID_dm_cnx_delay, 1);
	uint32_t min = attr_get_uint32(ATTR_ID_dm_cnx_delay_min, 1);
	uint32_t max = attr_get_uint32(ATTR_ID_dm_cnx_delay_max, 2);

	LOG_DBG("min: %u max: %u delay: %u", min, max, delay);

	if (delay == 0) {
		delay = RAND_RANGE(min, max);
		(void)attr_set_uint32(ATTR_ID_dm_cnx_delay, delay);
	}
}

static char *state_to_string(enum gw_dm_state state)
{
	switch (state) {
	case GW_DM_STATE_WAIT_FOR_NETWORK:
		return "Wait for network";
	case GW_DM_STATE_GET_NETWORK_TIME:
		return "Get network time";
	case GW_DM_STATE_POST_MEMFAULT_DATA:
		return "Post Memfault data";
	case GW_DM_STATE_WAIT_BEFORE_DM_CONNECTION:
		return "Delay before DM connection";
	case GW_DM_STATE_CONNECT_TO_DM:
		return "Connect to DM";
	case GW_DM_STATE_WAIT_FOR_CONNECTION:
		return "Wait for connection";
	case GW_DM_STATE_IDLE:
		return "Idle";
	case GW_DM_STATE_DISCONNECT_DM:
		return "Disconnect DM";
#if defined(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M)
	case GW_DM_STATE_CONNECT_TELEM:
		return "Connect to telemetry server";
	case GW_DM_STATE_WAIT_FOR_TELEM_CONNECTION:
		return "Wait for telemetry connection";
	case GW_DM_STATE_DISCONNECT_TELEM:
		return "Disconnect telemetry";
#endif
	default:
		return "Unknown";
	}
}

static void set_state(enum gw_dm_state next_state)
{
	if (next_state != gwto.state) {
		gwto.state = next_state;
		LOG_INF("%s", state_to_string(next_state));
	}
}

static bool timer_expired(void)
{
	if (gwto.timer > 0) {
		gwto.timer -= 1;
	}

	if (gwto.timer == 0) {
		return true;
	} else {
		return false;
	}
}

static void date_time_event_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_NTP:
		LOG_DBG("Got time from NTP");
		break;
	case DATE_TIME_NOT_OBTAINED:
		LOG_DBG("No time from NTP");
		break;
	default:
		break;
	}
}

static void gw_dm_fsm(void)
{
	int ret;
	char *ep_name;

	switch (gwto.state) {
	case GW_DM_STATE_WAIT_FOR_NETWORK:
		if (gwto.network_ready) {
			set_state(GW_DM_STATE_GET_NETWORK_TIME);
		} else if (timer_expired()) {
			gwto.network_ready = lcz_nm_network_ready();
			LOG_DBG("Re-checking network ready: %s",
				gwto.network_ready ? "true" : "false");
			gwto.timer = CONFIG_LCZ_BLE_GW_DM_WAIT_FOR_NETWORK_TIMEOUT;
		}
		break;
	case GW_DM_STATE_GET_NETWORK_TIME:
		if (!gwto.network_ready) {
			set_state(GW_DM_STATE_WAIT_FOR_NETWORK);
		} else {
			(void)date_time_update_async(date_time_event_handler);
			set_state(GW_DM_STATE_POST_MEMFAULT_DATA);
		}
		break;
	case GW_DM_STATE_POST_MEMFAULT_DATA:
		if (!gwto.network_ready) {
			set_state(GW_DM_STATE_WAIT_FOR_NETWORK);
		} else {
			LCZ_BLE_GW_DM_MEMFAULT_POST_DATA();
			gwto.timer = gwto.dm_connection_delay_seconds;
			set_state(GW_DM_STATE_WAIT_BEFORE_DM_CONNECTION);
			LOG_INF("Waiting %d seconds to connect to server",
				gwto.dm_connection_delay_seconds);
		}
		break;
	case GW_DM_STATE_WAIT_BEFORE_DM_CONNECTION:
		if (timer_expired()) {
			if (gwto.network_ready) {
				set_state(GW_DM_STATE_CONNECT_TO_DM);
			} else {
				set_state(GW_DM_STATE_WAIT_FOR_NETWORK);
			}
		}
		break;
	case GW_DM_STATE_CONNECT_TO_DM:
		if (!gwto.network_ready) {
			set_state(GW_DM_STATE_WAIT_FOR_NETWORK);
		} else {
			gwto.lwm2m_connection_err = false;
			gwto.timer = gwto.dm_connection_timeout_seconds;
#if defined(CONFIG_LCZ_LWM2M_CLIENT_ENABLE_ATTRIBUTES)
			ep_name = (char *)attr_get_quasi_static(ATTR_ID_lwm2m_endpoint);
#else
			ep_name = CONFIG_LCZ_LWM2M_CLIENT_ENDPOINT_NAME;
#endif
			ret = lcz_lwm2m_client_connect(CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX,
						       CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX,
						       CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX, ep_name,
						       LCZ_LWM2M_CLIENT_TRANSPORT_UDP,
						       CONFIG_LCZ_LWM2M_TLS_TAG);
			if (ret < 0) {
				set_state(GW_DM_STATE_WAIT_FOR_NETWORK);
			} else {
				set_state(GW_DM_STATE_WAIT_FOR_CONNECTION);
			}
		}
		break;
	case GW_DM_STATE_WAIT_FOR_CONNECTION:
		if (!gwto.network_ready) {
			set_state(GW_DM_STATE_DISCONNECT_DM);
		} else if (gwto.lwm2m_connection_err) {
			set_state(GW_DM_STATE_WAIT_FOR_NETWORK);
		} else {
			if (gwto.lwm2m_connected) {
#if defined(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M)
#if defined(CONFIG_LCZ_LWM2M_CLIENT_ENABLE_ATTRIBUTES)
				gwto.telem_enabled =
					*(bool *)attr_get_quasi_static(ATTR_ID_lwm2m_telem_enable);
				if (gwto.telem_enabled) {
					set_state(GW_DM_STATE_CONNECT_TELEM);
				} else {
					set_state(GW_DM_STATE_IDLE);
				}
#else
				set_state(GW_DM_STATE_CONNECT_TELEM);
#endif
#else
				set_state(GW_DM_STATE_IDLE);
#endif
			} else if (timer_expired()) {
				set_state(GW_DM_STATE_DISCONNECT_DM);
			}
		}
		break;
#if defined(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M)
	case GW_DM_STATE_CONNECT_TELEM:
		if (!gwto.network_ready) {
			set_state(GW_DM_STATE_DISCONNECT_DM);
		} else {
			gwto.lwm2m_connection_err = false;
			gwto.timer = gwto.dm_connection_timeout_seconds;
#if defined(CONFIG_LCZ_LWM2M_CLIENT_ENABLE_ATTRIBUTES)
			ep_name = (char *)attr_get_quasi_static(ATTR_ID_lwm2m_telem_endpoint);
#else
			ep_name = LCZ_BLE_GW_DM_TELEM_LWM2M_ENDPOINT_NAME;
#endif
			ret = lwm2m_telemetry_init();
			if (ret < 0) {
				break;
			}

			ret = lcz_lwm2m_client_connect(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_INDEX,
						       CONFIG_LCZ_BLE_GW_DM_TELEMETRY_SERVER_INST,
						       CONFIG_LCZ_BLE_GW_DM_TELEMETRY_SERVER_INST,
						       ep_name, LCZ_LWM2M_CLIENT_TRANSPORT_UDP,
						       CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M_TLS_TAG);
			if (ret < 0) {
				if (!lcz_lwm2m_client_is_connected(
					    CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX)) {
					set_state(GW_DM_STATE_DISCONNECT_DM);
				}
			} else {
				set_state(GW_DM_STATE_WAIT_FOR_TELEM_CONNECTION);
			}
		}
		break;
	case GW_DM_STATE_WAIT_FOR_TELEM_CONNECTION:
		if (!gwto.network_ready) {
			set_state(GW_DM_STATE_DISCONNECT_DM);
		} else if (gwto.lwm2m_connection_err) {
			if (!lcz_lwm2m_client_is_connected(CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX)) {
				set_state(GW_DM_STATE_DISCONNECT_DM);
			} else {
				set_state(GW_DM_STATE_DISCONNECT_TELEM);
			}
		} else {
			if (gwto.lwm2m_telem_connected) {
				set_state(GW_DM_STATE_IDLE);
			} else if (timer_expired()) {
				set_state(GW_DM_STATE_DISCONNECT_TELEM);
			}
		}
		break;
	case GW_DM_STATE_DISCONNECT_TELEM:
		if (!lcz_lwm2m_client_is_connected(CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX)) {
			set_state(GW_DM_STATE_DISCONNECT_DM);
		} else {
			if (!lcz_lwm2m_client_is_connected(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_INDEX)) {
				lcz_lwm2m_client_disconnect(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_INDEX,
							    false);
			} else {
				lcz_lwm2m_client_disconnect(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_INDEX,
							    true);
			}
			set_state(GW_DM_STATE_CONNECT_TELEM);
		}
		break;
#endif /* CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M */
	case GW_DM_STATE_IDLE:
		if (!gwto.network_ready || !gwto.lwm2m_connected) {
			set_state(GW_DM_STATE_DISCONNECT_DM);
		}
#if defined(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M)
		else if (gwto.telem_enabled && !gwto.lwm2m_telem_connected) {
			set_state(GW_DM_STATE_DISCONNECT_TELEM);
		}
#endif
		break;
	case GW_DM_STATE_DISCONNECT_DM:
		if (!lcz_lwm2m_client_is_connected(CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX)) {
			lcz_lwm2m_client_disconnect(CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX, false);
		} else {
			lcz_lwm2m_client_disconnect(CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX, true);
		}
#if defined(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M)
		if (!lcz_lwm2m_client_is_connected(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_INDEX)) {
			lcz_lwm2m_client_disconnect(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_INDEX, false);
		} else {
			lcz_lwm2m_client_disconnect(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_INDEX, true);
		}
#endif
		set_state(GW_DM_STATE_WAIT_FOR_NETWORK);
		break;
	default:
		break;
	}
}

static DispatchResult_t gateway_fsm_tick_handler(FwkMsgReceiver_t *pMsgRxer, FwkMsg_t *pMsg)
{
	gw_dm_fsm();
	Framework_StartTimer(&gwto.msgTask);
	return DISPATCH_OK;
}

static FwkMsgHandler_t *gw_dm_task_msg_dispatcher(FwkMsgCode_t MsgCode)
{
	/* clang-format off */
	switch (MsgCode) {
	case FMC_INVALID:                    return Framework_UnknownMsgHandler;
	case FMC_PERIODIC:                   return gateway_fsm_tick_handler;
	case FMC_ATTR_CHANGED:               return attr_broadcast_msg_handler;
	default:                             return NULL;
	}
	/* clang-format on */
}

static void nm_event_callback(enum lcz_nm_event event)
{
	LOG_DBG("Network monitor event %d", event);
	switch (event) {
	case LCZ_NM_EVENT_IFACE_DOWN:
		gwto.network_ready = false;
		FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_BLE_GW_DM, FMC_NETWORK_DISCONNECTED);
		break;
	case LCZ_NM_EVENT_IFACE_DNS_ADDED:
		gwto.network_ready = true;
		FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_BLE_GW_DM, FMC_NETWORK_CONNECTED);
		break;
	default:
		break;
	}
}

static void lwm2m_client_connected_event(struct lwm2m_ctx *client, int lwm2m_client_index,
					 bool connected, enum lwm2m_rd_client_event client_event)
{
	gwto.lwm2m_connection_err = false;

	if (lwm2m_client_index == CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX) {
		gwto.lwm2m_connected = connected;
	}
#if defined(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M)
	else {
		gwto.lwm2m_telem_connected = connected;
	}
#endif

	if (lwm2m_client_index == CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX) {
		switch (client_event) {
		case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
			pet_connection_watchdog(true);
			break;
		case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
			pet_connection_watchdog(true);
			break;
		case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
			pet_connection_watchdog(false);
			break;
		case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE:
		case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
		case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
			gwto.lwm2m_connection_err = true;
			break;
		default:
			break;
		}
	}
#if defined(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M)
	else {
		switch (client_event) {
		case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE:
		case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
		case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
			gwto.lwm2m_connection_err = true;
			break;
		default:
			break;
		}
	}
#endif
}

static void connection_watchdog_timer_callback(struct k_timer *timer_id)
{
	if (timer_id == &connection_watchdog_timer) {
		LOG_WRN("Connection watchdog expired!");
		MFLT_METRICS_ADD(lwm2m_dm_watchdog, 1);
		lcz_lwm2m_client_disconnect(CONFIG_LCZ_BLE_GW_DM_CLIENT_INDEX, false);
	} else if (timer_id == &connection_watchdog_reboot_timer) {
		LOG_WRN("Connection reboot watchdog expired!");
		lcz_software_reset_after_assert(CONNECTION_WATCHDOG_REBOOT_DELAY_MS);
	}
}

static void pet_connection_watchdog(bool in_connection)
{
	int ret;
	uint32_t timeout;

	if (in_connection) {
		ret = lwm2m_engine_get_u32("1/0/1", &timeout);
		if (ret < 0) {
			LOG_ERR("Could not read lifetime");
		}
		/* Wait for multiple registration updates */
		timeout *= CONNECTION_WATCHDOG_TIMEOUT_MULTIPLIER;

		/* pet the reboot watchdog to prevent a system reboot */
		k_timer_start(&connection_watchdog_reboot_timer,
			      K_MINUTES(CONNECTION_WATCHDOG_REBOOT_TIMER_TIMEOUT_MINUTES),
			      K_NO_WAIT);
	} else {
		timeout =
			attr_get_uint32(ATTR_ID_dm_cnx_delay_max, CONNECTION_WATCHDOG_MAX_FALLBACK);
		timeout *= CONNECTION_WATCHDOG_TIMEOUT_MULTIPLIER;
	}
	LOG_DBG("Connection watchdog set to %d seconds", timeout);
	k_timer_start(&connection_watchdog_timer, K_SECONDS(timeout), K_NO_WAIT);
}

static void *current_time_read_cb(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id,
				  size_t *data_len)
{
	struct timespec tp;

	ARG_UNUSED(obj_inst_id);
	ARG_UNUSED(res_id);
	ARG_UNUSED(res_inst_id);

	clock_gettime(CLOCK_REALTIME, &tp);
	gwto.time = tp.tv_sec;
	*data_len = 4;
	LOG_DBG("Epoch time: %d", gwto.time);

	return &gwto.time;
}

static void ble_gw_dm_thread(void *arg1, void *arg2, void *arg3)
{
	LOG_INF("BLE Gateway Device Manager Started");
#if defined(CONFIG_ATTR)
	char *dev_id;
	dev_id = (char *)attr_get_quasi_static(ATTR_ID_device_id);
	if (strlen(dev_id) < 1) {
#if defined(CONFIG_MODEM_HL7800)
		dev_id = mdm_hl7800_get_imei();
#else
		dev_id = (char *)attr_get_quasi_static(ATTR_ID_bluetooth_address);
#endif
		attr_set_string(ATTR_ID_device_id, dev_id, strlen(dev_id));
	}
#endif

	k_timer_init(&connection_watchdog_timer, connection_watchdog_timer_callback, NULL);
	k_timer_init(&connection_watchdog_reboot_timer, connection_watchdog_timer_callback, NULL);
	/* Start the reboot watchdog to reboot the system if we never connect to the server. */
	k_timer_start(&connection_watchdog_reboot_timer,
		      K_MINUTES(CONNECTION_WATCHDOG_REBOOT_TIMER_TIMEOUT_MINUTES), K_NO_WAIT);

	gwto.network_ready = false;
	event_agent.callback = nm_event_callback;
	lcz_nm_register_event_callback(&event_agent);

	gwto.msgTask.rxer.id = FWK_ID_BLE_GW_DM;
	gwto.msgTask.rxer.rxBlockTicks = K_FOREVER;
	gwto.msgTask.rxer.pMsgDispatcher = gw_dm_task_msg_dispatcher;
	gwto.msgTask.timerDurationTicks = K_SECONDS(1);
	gwto.msgTask.timerPeriodTicks = K_MSEC(0);
	gwto.msgTask.rxer.pQueue = &gw_dm_task_queue;

	set_state(GW_DM_STATE_WAIT_FOR_NETWORK);
	gwto.timer = CONFIG_LCZ_BLE_GW_DM_WAIT_FOR_NETWORK_TIMEOUT;
	gwto.dm_connection_timeout_seconds = CONFIG_LCZ_BLE_GW_DM_CONNECTION_TIMEOUT;

#if defined(CONFIG_LCZ_BLE_GW_DM_INIT_KCONFIG)
	gwto.dm_connection_delay_seconds = CONFIG_LCZ_BLE_GW_DM_CONNECTION_DELAY;
#else
	/* generate random connect time if value is 0 */
	random_connect_handler();
	gwto.dm_connection_delay_seconds =
		attr_get_uint32(ATTR_ID_dm_cnx_delay, DM_CONNECTION_DELAY_FALLBACK);
#endif

	lwm2m_event_agent.connected_callback = lwm2m_client_connected_event;
	(void)lcz_lwm2m_client_register_event_callback(&lwm2m_event_agent);
	lcz_lwm2m_client_register_get_time_callback(current_time_read_cb);

	Framework_RegisterTask(&gwto.msgTask);
	Framework_StartTimer(&gwto.msgTask);

	while (true) {
		Framework_MsgReceiver(&gwto.msgTask.rxer);
	}

	LOG_ERR("BLE Gateway Device Manager Exited!");
}

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
K_THREAD_DEFINE(ble_gw_dm, CONFIG_LCZ_BLE_GW_DM_THREAD_STACK_SIZE, ble_gw_dm_thread, NULL, NULL,
		NULL, K_PRIO_PREEMPT(CONFIG_LCZ_BLE_GW_DM_THREAD_PRIORITY), 0, 0);
