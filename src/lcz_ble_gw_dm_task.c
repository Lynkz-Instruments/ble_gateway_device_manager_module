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

#include "fwk_includes.h"
#include "lcz_network_monitor.h"

#include "lcz_ble_gw_dm_task.h"

/**************************************************************************************************/
/* Local Constant, Macro and Type Definitions                                                     */
/**************************************************************************************************/
#define GW_DM_TASK_QUEUE_DEPTH 8

enum gw_dm_state {
	GW_DM_STATE_WAIT_FOR_NETWORK = 0,
	GW_DM_STATE_WAIT_BEFORE_DM_CONNECTION,
	GW_DM_STATE_CONNECT_TO_DM,
	GW_DM_STATE_DISCONNECT_DM,
};

typedef struct gw_dm_task_obj {
	FwkMsgTask_t msgTask;
	enum gw_dm_state state;
	uint32_t timer;
	bool network_ready;
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

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
static struct lcz_nm_event_agent event_agent;
static gw_dm_task_obj_t gwto;
K_MSGQ_DEFINE(gw_dm_task_queue, FWK_QUEUE_ENTRY_SIZE, GW_DM_TASK_QUEUE_DEPTH, FWK_QUEUE_ALIGNMENT);
static uint32_t dm_connection_delay_seconds;

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/

static char *state_to_string(enum gw_dm_state state)
{
	switch (state) {
	case GW_DM_STATE_WAIT_FOR_NETWORK:
		return "Wait for network";
	case GW_DM_STATE_WAIT_BEFORE_DM_CONNECTION:
		return "Delay before DM connection";
	case GW_DM_STATE_CONNECT_TO_DM:
		return "Connect to DM";
	case GW_DM_STATE_DISCONNECT_DM:
		return "Disconnect DM";
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

static void gw_dm_fsm(void)
{
	switch (gwto.state) {
	case GW_DM_STATE_WAIT_FOR_NETWORK:
		gwto.network_ready = lcz_nm_network_ready();
		if (gwto.network_ready) {
			FRAMEWORK_MSG_CREATE_AND_BROADCAST(FWK_ID_BLE_GW_DM, FMC_NETWORK_CONNECTED);
			set_state(GW_DM_STATE_WAIT_BEFORE_DM_CONNECTION);
		} else {
			gwto.timer = dm_connection_delay_seconds;
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
			/* connect to DM server */
		}
		break;
	case GW_DM_STATE_DISCONNECT_DM:
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
	default:
		break;
	}
}

static void ble_gw_dm_thread(void *arg1, void *arg2, void *arg3)
{
	LOG_INF("BLE Gateway Device Manager Started");

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
#if defined(CONFIG_LCZ_BLE_GW_DM_CONNECTION_DELAY)
	dm_connection_delay_seconds = CONFIG_LCZ_BLE_GW_DM_CONNECTION_DELAY;
#else
	dm_connection_delay_seconds = 0;
#endif

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
