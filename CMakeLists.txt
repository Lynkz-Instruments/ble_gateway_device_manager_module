#
# Copyright (c) 2022 Laird Connectivity LLC
#
# SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
#
if(CONFIG_LCZ_BLE_GW_DM)

zephyr_include_directories(include)

zephyr_sources(src/lcz_ble_gw_dm_task.c)

endif()
