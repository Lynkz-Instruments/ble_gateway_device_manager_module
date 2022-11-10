/**
 * @file memfault_task.h
 * @brief A thread is used to facilitate sending Memfault data on demand and periodically.
 * It is not possible to use the system workq for this task because POSTing the data blocks.
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#ifndef __MEMFAULT_TASK_H__
#define __MEMFAULT_TASK_H__

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************/
/* Global Constants, Macros and Type Definitions                                                  */
/**************************************************************************************************/
#ifdef CONFIG_LCZ_BLE_GW_DM_MEMFAULT
#define LCZ_BLE_GW_DM_MEMFAULT_POST_DATA lcz_ble_gw_dm_memfault_post_data
#define LCZ_BLE_GW_DM_MEMFAULT_POST_DATA_SYNC lcz_ble_gw_dm_memfault_post_data_sync
#else
#define LCZ_BLE_GW_DM_MEMFAULT_POST_DATA(...)
#define LCZ_BLE_GW_DM_MEMFAULT_POST_DATA_SYNC(...)
#endif

/**************************************************************************************************/
/* Global Function Prototypes                                                                     */
/**************************************************************************************************/
#ifdef CONFIG_LCZ_BLE_GW_DM_MEMFAULT
/**
 * @brief Post any available data to memfault cloud via HTTPS
 * NOTE: This is a non-blocking call that signals the task to send data asynchronously.
 *
 * @return 0 on success
 */
int lcz_ble_gw_dm_memfault_post_data(void);

/**
 * @brief Post any available data to memfault cloud via HTTPS synchronously.
 * NOTE: This is a blocking call that returns once data has been sent.
 *
 * @return 0 on success
 */
int lcz_ble_gw_dm_memfault_post_data_sync(void);
#endif /* CONFIG_LCZ_BLE_GW_DM_MEMFAULT*/

#ifdef __cplusplus
}
#endif

#endif /* __MEMFAULT_TASK_H__ */
