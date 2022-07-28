/**
 * @file lwm2m_telemetry.c
 *
 * Copyright (c) 2022 Laird Connectivity LLC
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lwm2m_telemetry, CONFIG_LCZ_BLE_GW_DM_LOG_LEVEL);

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <zephyr.h>
#include "lcz_lwm2m_client.h"
#if defined(CONFIG_ATTR)
#include "attr.h"
#endif

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
int lwm2m_telemetry_init(void)
{
	int ret;
	char *server_url;
	lcz_lwm2m_client_security_mode_t sec_mode;
	char *psk_id;
	uint8_t *psk;
#if defined(CONFIG_LCZ_BLE_GW_DM_INIT_KCONFIG)
	uint8_t psk_bin[CONFIG_LCZ_LWM2M_SECURITY_KEY_SIZE];
#endif
	uint16_t short_server_id;

#if defined(CONFIG_LCZ_BLE_GW_DM_INIT_KCONFIG)
	server_url = CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M_SERVER_URL;
	sec_mode = (lcz_lwm2m_client_security_mode_t)CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M_SECURITY_MODE;
	psk_id = CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M_PSK_ID;
	ret = hex2bin(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M_PSK,
		      strlen(CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M_PSK), psk_bin, sizeof(psk_bin));
	if (ret == 0 || ret != sizeof(psk_bin)) {
		LOG_ERR("Could not convert PSK to binary");
		goto exit;
	}
	psk = psk_bin;
#else
	server_url = (char *)attr_get_quasi_static(ATTR_ID_lwm2m_telem_server_url);
	ret = attr_get(ATTR_ID_lwm2m_telem_security, &sec_mode, sizeof(sec_mode));
	if (ret < 0) {
		goto exit;
	}
	psk_id = (char *)attr_get_quasi_static(ATTR_ID_lwm2m_telem_psk_id);
	psk = (uint8_t *)attr_get_quasi_static(ATTR_ID_lwm2m_telem_psk);
#endif

	ret = lcz_lwm2m_client_set_server_url(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_SERVER_INST,
					      server_url, strlen(server_url));
	if (ret < 0) {
		goto exit;
	}

	ret = lcz_lwm2m_client_set_security_mode(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_SERVER_INST,
						 sec_mode);
	if (ret < 0) {
		goto exit;
	}

	if (sec_mode == LCZ_LWM2M_CLIENT_SECURITY_MODE_PSK) {
		ret = lcz_lwm2m_client_set_key_or_id(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_SERVER_INST,
						     psk_id, strlen(psk_id));
		if (ret < 0) {
			goto exit;
		}

		ret = lcz_lwm2m_client_set_secret_key(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_SERVER_INST,
						      psk, CONFIG_LCZ_LWM2M_SECURITY_KEY_SIZE);
		if (ret < 0) {
			goto exit;
		}
	}

#if defined(CONFIG_LCZ_BLE_GW_DM_INIT_KCONFIG)
	short_server_id = CONFIG_LCZ_BLE_GW_DM_TELEM_LWM2M_SHORT_SERVER_ID;
#else
	ret = attr_get(ATTR_ID_lwm2m_telem_short_id, &short_server_id, sizeof(short_server_id));
	if (ret < 0) {
		goto exit;
	}
#endif
	ret = lcz_lwm2m_client_set_bootstrap(CONFIG_LCZ_BLE_GW_DM_TELEMETRY_INDEX,
					     CONFIG_LCZ_BLE_GW_DM_TELEMETRY_SERVER_INST, false,
					     short_server_id);
	if (ret < 0) {
		goto exit;
	}

	LOG_DBG("LwM2M telemetry client initialized");
exit:
	return ret;
}
