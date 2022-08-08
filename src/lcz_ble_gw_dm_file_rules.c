/**
 * @file lcz_ble_gw_dm_file_rules.c
 * @brief Holds rules for file access for the DM gateway
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lcz_ble_gw_dm_file_rules, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr.h>
#include <init.h>

#include "file_system_utilities.h"
#include "encrypted_file_storage.h"

#if defined(CONFIG_ATTR)
#include "attr.h"
#endif
#if defined(CONFIG_LCZ_FS_MGMT_FILE_ACCESS_HOOK)
#include <lcz_fs_mgmt/lcz_fs_mgmt.h>
#endif
#if defined(CONFIG_LCZ_LWM2M_FS_MANAGEMENT)
#include "lcz_lwm2m_obj_fs_mgmt.h"
#endif

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static int lcz_ble_gw_dm_file_rules_init(const struct device *device);
static bool gw_dm_file_test(const char *path, bool write);

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
static bool gw_dm_file_test(const char *path, bool write)
{
	char simple_path[FSU_MAX_ABS_PATH_SIZE + 1];
	char *load_path;

	/* Simplify the path */
	if (fsu_simplify_path(path, simple_path) < 0) {
		/* If the simplification failed, deny access */
		return false;
	}

	/* Allow any reads and writes to non-encrypted paths */
	if (efs_is_encrypted_path(simple_path) == false) {
		return true;
	}

	/* Reads of encrypted files are not allowed */
	if (write == false) {
		return false;
	}

	/* Write of the attribute load file is allowed */
#ifdef ATTR_ID_load_path
	load_path = (char *)attr_get_quasi_static(ATTR_ID_load_path);
	if (strcmp(load_path, simple_path) == 0) {
		return true;
	}
#endif

	/* If the factory load file does not exist, it can be written */
#ifdef ATTR_ID_factory_load_path
	load_path = (char *)attr_get_quasi_static(ATTR_ID_factory_load_path);
	if (strcmp(load_path, simple_path) == 0) {
		if (efs_get_file_size(simple_path) < 0) {
			return true;
		}
	}
#endif

	/* Reject anything else by default */
	return false;
}

/**************************************************************************************************/
/* SYS INIT                                                                                       */
/**************************************************************************************************/
SYS_INIT(lcz_ble_gw_dm_file_rules_init, APPLICATION, CONFIG_LCZ_GW_DM_FILE_RULES_INIT_PRIORITY);
static int lcz_ble_gw_dm_file_rules_init(const struct device *device)
{
	/* Register our rules function with SMP and LwM2M */
#if defined(CONFIG_LCZ_FS_MGMT_FILE_ACCESS_HOOK)
	lcz_fs_mgmt_register_evt_cb(gw_dm_file_test);
#endif
#if defined(CONFIG_LCZ_LWM2M_FS_MANAGEMENT)
	lcz_lwm2m_obj_fs_mgmt_register_cb(gw_dm_file_test);
#endif
	return 0;
}
