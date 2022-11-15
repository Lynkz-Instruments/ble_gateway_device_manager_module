/**
 * @file lcz_ble_gw_dm_file_rules.c
 * @brief Holds rules for file access for the DM gateway
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lcz_ble_gw_dm_file_rules, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr/zephyr.h>
#include <zephyr/init.h>

#include <file_system_utilities.h>
#include <encrypted_file_storage.h>

#if defined(CONFIG_ATTR)
#include <attr.h>
#endif
#if defined(CONFIG_LCZ_PKI_AUTH)
#include <lcz_pki_auth.h>
#endif
#if defined(CONFIG_LCZ_FS_MGMT_FILE_ACCESS_HOOK)
#include <lcz_fs_mgmt/lcz_fs_mgmt.h>
#endif
#if defined(CONFIG_LCZ_LWM2M_FS_MANAGEMENT)
#include <lcz_lwm2m_obj_fs_mgmt.h>
#endif
#if defined(CONFIG_LCZ_SHELL_SCRIPT_RUNNER)
#include <lcz_shell_script_runner.h>
#endif
#if defined(CONFIG_LCZ_LWM2M_FW_UPDATE_SHELL)
#include <lcz_lwm2m_fw_update.h>
#endif

/**************************************************************************************************/
/* Local Constant, Macro and Type Definitions                                                     */
/**************************************************************************************************/
#ifdef ATTR_ID_factory_load_path
/* How long after the last write will writes to the factory load path still succeed */
#define FACTORY_WRITE_DURATION K_SECONDS(1)
#endif

struct exec_queue_entry_t {
	void *fifo_reserved;
	char path[FSU_MAX_ABS_PATH_SIZE + 1];
};

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
#if defined(CONFIG_LCZ_PKI_AUTH)
static bool is_key_file(char *path);
#endif
static int lcz_ble_gw_dm_file_rules_init(const struct device *device);
static bool gw_dm_file_test(const char *path, bool write);
static void factory_write_work_handler(struct k_work *work);
static int gw_dm_file_exec(const char *path);
static void exec_work_handler(struct k_work *work);

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
#ifdef ATTR_ID_factory_load_path
static K_WORK_DELAYABLE_DEFINE(factory_write_work, factory_write_work_handler);
#endif
static K_FIFO_DEFINE(exec_queue);
static K_WORK_DEFINE(exec_work, exec_work_handler);

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
#if defined(CONFIG_LCZ_PKI_AUTH)
static bool is_key_file(char *path)
{
	LCZ_PKI_AUTH_STORE_T store;
	char key_fname[FSU_MAX_ABS_PATH_SIZE + 1];

	for (store = LCZ_PKI_AUTH_STORE_DEVICE_MANAGEMENT; store < LCZ_PKI_AUTH_STORE__NUM;
	     store++) {
		if (lcz_pki_auth_file_name_get(store, LCZ_PKI_AUTH_FILE_PRIVATE_KEY, key_fname,
					       sizeof(key_fname)) == 0) {
			if (strcmp(path, key_fname) == 0) {
				return true;
			}
		}
		if (lcz_pki_auth_file_name_get(store, LCZ_PKI_AUTH_FILE_PUBLIC_KEY, key_fname,
					       sizeof(key_fname)) == 0) {
			if (strcmp(path, key_fname) == 0) {
				return true;
			}
		}
	}

	return false;
}
#endif

static bool gw_dm_file_test(const char *path, bool write)
{
	char simple_path[FSU_MAX_ABS_PATH_SIZE + 1];
	char *load_path;

	/* Simplify the path */
	if (fsu_simplify_path(path, simple_path) < 0) {
		/* If the simplification failed, deny access */
		return false;
	}

	/* If the file doesn't start with the mount path, reject it */
	if (strncmp(simple_path, CONFIG_FSU_MOUNT_POINT, strlen(CONFIG_FSU_MOUNT_POINT)) != 0)
	{
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
#if defined(ATTR_ID_load_path)
	load_path = (char *)attr_get_quasi_static(ATTR_ID_load_path);
	if (strcmp(load_path, simple_path) == 0) {
		return true;
	}
#endif

	/* If the factory load file does not exist, it can be written */
#if defined(ATTR_ID_factory_load_path)
	load_path = (char *)attr_get_quasi_static(ATTR_ID_factory_load_path);
	if (strcmp(load_path, simple_path) == 0) {
		if (k_work_delayable_is_pending(&factory_write_work)) {
			/* Write of the factory file is pending */
			k_work_reschedule(&factory_write_work, FACTORY_WRITE_DURATION);
			return true;
		} else if (efs_get_file_size(simple_path) < 0) {
			/* New write of factory file is allowed */
			k_work_reschedule(&factory_write_work, FACTORY_WRITE_DURATION);
			return true;
		}
	}
#endif

	/* Writes of private/public key files are allowed */
#if defined(CONFIG_LCZ_PKI_AUTH)
	if (is_key_file(simple_path)) {
		return true;
	}
#endif

	/* Reject anything else by default */
	return false;
}

static void factory_write_work_handler(struct k_work *work)
{
	/* Nothing to do */
}

static int gw_dm_file_exec(const char *path)
{
	char simple_path[FSU_MAX_ABS_PATH_SIZE + 1];
	char *attr_path;
	char *fstr = NULL;
	struct exec_queue_entry_t *entry;
	int ret;

	/* Simplify the path */
	if (fsu_simplify_path(path, simple_path) < 0) {
		/* If the simplification failed, say we failed */
		return -EINVAL;
	}

	/* Attempting to execute the attribute load path will load attributes */
#if defined(ATTR_ID_load_path)
	attr_path = (char *)attr_get_quasi_static(ATTR_ID_load_path);
	if (strcmp(attr_path, simple_path) == 0) {
		ret = attr_load(simple_path, NULL);
		if (ret > 0) {
			lcz_lwm2m_obj_fs_mgmt_exec_complete(0);
			return 0;
		} else if (ret == 0) {
			return -ENOENT;
		} else {
			return ret;
		}
	}
#endif

	/* Do not allow the factory load path to work the same way */
#if defined(ATTR_ID_factory_load_path)
	attr_path = (char *)attr_get_quasi_static(ATTR_ID_factory_load_path);
	if (strcmp(attr_path, simple_path) == 0) {
		/* Use the factory reset execute from Object 3 instead */
		return -EPERM;
	}
#endif

	/* Attempting to execute the attribute dump path will dump attributes */
#if defined(ATTR_ID_dump_path)
	attr_path = (char *)attr_get_quasi_static(ATTR_ID_dump_path);
	if (strcmp(attr_path, simple_path) == 0) {
		ret = attr_prepare_then_dump(&fstr, ATTR_DUMP_RW);
		if (ret > 0) {
			if (fsu_write_abs(simple_path, fstr, strlen(fstr)) > 0) {
				lcz_lwm2m_obj_fs_mgmt_exec_complete(0);
				ret = 0;
			} else {
				ret = -ENOENT;
			}
			k_free(fstr);
			return ret;
		} else if (ret == 0) {
			return -ENOENT;
		} else {
			return ret;
		}
	}
#endif

	/* Allow shell scripts to be executed */
#if defined(CONFIG_LCZ_SHELL_SCRIPT_RUNNER)
	if (lcz_zsh_is_script(simple_path) == true) {
		/* Create a queue entry for this script execution */
		entry = (struct exec_queue_entry_t *)k_malloc(sizeof(struct exec_queue_entry_t));
		if (entry == NULL) {
			/* Fail if memory couldn't be allocated */
			return -ENOMEM;
		} else {
			/* Add the script to the queue */
			memcpy(entry->path, simple_path, sizeof(simple_path));
			k_fifo_put(&exec_queue, entry);

			/* Schedule the work */
			k_work_submit(&exec_work);

			/* Success, for now */
			return 0;
		}
	}
#endif

	/* If everything above didn't work, the execute wasn't allowed */
	return -EPERM;
}

static void exec_work_handler(struct k_work *work)
{
	struct exec_queue_entry_t *entry;
	int ret;

	/* Pull an entry off of the fifo */
	entry = k_fifo_get(&exec_queue, K_FOREVER);
	if (entry != NULL) {
		/* Execute the script */
		ret = lcz_zsh_run_script(entry->path, NULL);

		/* Call the complete callback */
		lcz_lwm2m_obj_fs_mgmt_exec_complete(ret);

		/* Free the entry */
		k_free(entry);

		/* If there are more things to execute, reschedule the work */
		if (!k_fifo_is_empty(&exec_queue)) {
			k_work_submit(&exec_work);
		}
	}
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
	lcz_lwm2m_obj_fs_mgmt_reg_perm_cb(gw_dm_file_test);
	lcz_lwm2m_obj_fs_mgmt_reg_exec_cb(gw_dm_file_exec);
#endif
#if defined(CONFIG_LCZ_LWM2M_FW_UPDATE_SHELL)
	lcz_lwm2m_fw_update_shell_reg_perm_cb(gw_dm_file_test);
#endif
	return 0;
}
