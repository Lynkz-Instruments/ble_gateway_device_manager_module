/**
 * @file led_config.h
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */
#ifndef __LED_CONFIG_H__
#define __LED_CONFIG_H__

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <lcz_led.h>

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************/
/* Global Constants, Macros and Type Definitions                                                  */
/**************************************************************************************************/

#if defined(CONFIG_BOARD_PINNACLE_100_DVK) || defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
#define NUM_LEDS 4
#elif defined(CONFIG_BOARD_MG100)
#define NUM_LEDS 3
#elif defined(CONFIG_BOARD_NRF7002DK_NRF5340_CPUAPP)
#define NUM_LEDS 2
#else
#error "Undefined board"
#endif


#if (NUM_LEDS >= 1)
#define LED1_NODE DT_ALIAS(led0)
#define LED1_DEV 	DEVICE_DT_GET(DT_GPIO_CTLR(LED1_NODE, gpios))
#define LED1_FLAGS 	DT_GPIO_FLAGS(LED1_NODE, gpios)
#define LED1 		DT_GPIO_PIN(LED1_NODE, gpios)
#endif

#if (NUM_LEDS >= 2)
#define LED2_NODE DT_ALIAS(led1)
#define LED2_DEV	DEVICE_DT_GET(DT_GPIO_CTLR(LED2_NODE, gpios))
#define LED2_FLAGS 	DT_GPIO_FLAGS(LED2_NODE, gpios)
#define LED2 		DT_GPIO_PIN(LED2_NODE, gpios)
#endif

#if (NUM_LEDS >= 3)
#define LED3_NODE DT_ALIAS(led2)
#define LED3_DEV 	DEVICE_DT_GET(DT_GPIO_CTLR(LED3_NODE, gpios))
#define LED3_FLAGS 	DT_GPIO_FLAGS(LED3_NODE, gpios)
#define LED3 		DT_GPIO_PIN(LED3_NODE, gpios)
#endif

#if (NUM_LEDS >= 4)
#define LED4_NODE DT_ALIAS(led3)
#define LED4_DEV 	DEVICE_DT_GET(DT_GPIO_CTLR(LED4_NODE, gpios))
#define LED4_FLAGS 	DT_GPIO_FLAGS(LED4_NODE, gpios)
#define LED4 		DT_GPIO_PIN(LED4_NODE, gpios)
#endif

#if defined(CONFIG_BOARD_PINNACLE_100_DVK) || defined(CONFIG_BOARD_MG100)
enum led_index {
	BLUE_LED = 0,
	GREEN_LED,
	RED_LED,
#if defined(CONFIG_BOARD_PINNACLE_100_DVK)
	GREEN_LED2
#endif
};

enum led_type_index {
	NETWORK_LED = RED_LED,
	DM_LED = GREEN_LED,
	BLE_LED = BLUE_LED,
};

#if defined(CONFIG_BOARD_PINNACLE_100_DVK)
BUILD_ASSERT(CONFIG_LCZ_NUMBER_OF_LEDS > GREEN_LED2, "LED object too small");
#else
BUILD_ASSERT(CONFIG_LCZ_NUMBER_OF_LEDS > RED_LED, "LED object too small");
#endif

#elif defined(CONFIG_BOARD_BL5340_DVK_CPUAPP)
enum led_index {
	BLUE_LED1 = 0,
	BLUE_LED2,
	BLUE_LED3,
	BLUE_LED4,
};

enum led_type_index {
	NETWORK_LED = BLUE_LED1,
	DM_LED = BLUE_LED2,
	BLE_LED = BLUE_LED3,
};

BUILD_ASSERT(CONFIG_LCZ_NUMBER_OF_LEDS > BLUE_LED4, "LED object too small");
#elif defined(CONFIG_BOARD_NRF7002DK_NRF5340_CPUAPP)
enum led_index {
	GREEN_LED1 = 0,
	GREEN_LED2,
};

enum led_type_index {
	NETWORK_LED = GREEN_LED1,
	DM_LED = GREEN_LED2,
	BLE_LED = -1,
};

BUILD_ASSERT(CONFIG_LCZ_NUMBER_OF_LEDS > GREEN_LED2, "LED object too small");
#else
#error "Unsupported board selected"
#endif

/* clang-format off */
static const struct lcz_led_blink_pattern NETWORK_SEARCH_LED_PATTERN = {
	.on_time = 40,
	.off_time = 80,
	.repeat_count = 2
};

static const struct lcz_led_blink_pattern BLE_ACTIVITY_LED_PATTERN = {
	.on_time = 30,
	.off_time = 20,
	.repeat_count = 0
};
/* clang-format on */

#ifdef __cplusplus
}
#endif

#endif /* __LED_CONFIG_H__ */
