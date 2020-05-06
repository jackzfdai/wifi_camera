/*
 * Data structures and functions to implement system state machine
 *
 * Author: Jack Dai
 * Date: October 18, 2019
 */

/*-------------------------------------------------------------------------------------------------
 * Includes
 --------------------------------------------------------------------------------------------------*/
#include <state_machine_old.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/adc.h"

#include "bsp.h"
#include "system_mgmt.h"
#include "camera_module.h"
#include "network_module.h"

#include "esp_camera.h"
#include "esp_log.h"

/*-------------------------------------------------------------------------------------------------
 * Defines
 --------------------------------------------------------------------------------------------------*/
static const char* TAG = "sys_mgmt";
/*-------------------------------------------------------------------------------------------------
 * Typedefs
 --------------------------------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------------------------------
 * Private Function/State Declarations
 --------------------------------------------------------------------------------------------------*/
static state_id_t state_idle (event_t event);

static state_id_t state_camera_active (event_t event);

static state_id_t state_fault (event_t event);

static state_id_t state_disable (event_t event);

//static void check_light_lvl_task(void *arg);

/*-------------------------------------------------------------------------------------------------
 * Variables
 --------------------------------------------------------------------------------------------------*/
hsm_handle_t hsm_system_mgmt;

static StaticQueue_t system_mgmt_queue;

static event_t system_mgmt_queue_buffer[SYSTEM_MGMT_EVENT_QUEUE_SIZE];

state_t state_table[] = {
		{STATE_IDLE, &state_idle},
		{STATE_CAMERA_ACTIVE, &state_camera_active},
		{STATE_FAULT, &state_fault},
		{STATE_DISABLE, &state_disable},
};
/*-------------------------------------------------------------------------------------------------
 * Public Function Definitions
 --------------------------------------------------------------------------------------------------*/
esp_err_t system_mgmt_init(hsm_handle_t *super_state_hsm) //, state_t *super_state)
{
	printf("sys mgmt init start");
	esp_err_t ret_code = ESP_FAIL;

	hsm_init_t init;

	init.event_queue = system_mgmt_queue;
	init.event_queue_buffer = system_mgmt_queue_buffer;
	init.max_num_events = SYSTEM_MGMT_EVENT_QUEUE_SIZE;
	init.num_states = STATE_COUNT(state_table);
	init.state_table = state_table;
	init.watchdog_task_id = 0;
	init.EVENT_STATE_ENTRY = EVENT_STATE_ENTRY;
	init.EVENT_DISABLE = EVENT_DISABLE;
	init.EVENT_ENABLE = EVENT_ENABLE;
	init.STATE_DISABLE = &state_table[init.num_states - 1];
	init.STATE_ID_ANY = STATE_ANY;

	printf("bp 1 s state %p\n", get_s_state());

	ESP_ERROR_CHECK(hsm_init(&init, &hsm_system_mgmt, super_state_hsm)); //, super_state));

	printf("bp 2 s state %p\n", get_s_state());

	printf("sys mgmt init suc, %p\n", init.event_queue_buffer);

	xTaskCreate(&system_mgmt_task, "system_mgmt", 4096, NULL, 5, NULL); //TODO: error check this

//	printf("bp 3 s state %p\n", get_s_state());
	ESP_LOGI(TAG, "free heap before camera init: %d\n", xPortGetFreeHeapSize());

	ret_code = camera_module_init();
	if (ret_code != ESP_OK)
	{
		printf("camera module init failed\n");
		return ret_code;
	}

	ESP_LOGI(TAG, "free heap after camera init: %d\n", xPortGetFreeHeapSize());

	ret_code = network_module_init();
	if (ret_code != ESP_OK)
	{
		printf("network module init failed\n");
		return ret_code;
	}

	ESP_LOGI(TAG, "free heap after network init: %d\n", xPortGetFreeHeapSize());

	ret_code = hsm_send_evt(&hsm_system_mgmt, EVENT_STARTUP_COMPLETE, portMAX_DELAY); //portMAX_DELAY

	ret_code = ESP_OK;
		
	return ret_code;
}

void system_mgmt_task(void *pvParameter)
{
	printf("system mgmt task start\n");

	hsm_task(&hsm_system_mgmt);

	//should not get here
	while(1)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

void watch_dog_task(void *pvParameter)
{
	esp_err_t ret_code = ESP_OK;
	while(1)
	{
		ret_code = hsm_send_evt(&hsm_system_mgmt, WDG_EVT, portMAX_DELAY); //portMAX_DELAY
		printf("wdg send result %d\n", ret_code);
		vTaskDelay(1000/portTICK_PERIOD_MS);//pdMS_TO_TICKS(1000));
	}
}
/*-------------------------------------------------------------------------------------------------
 * Private Function Definitions
 --------------------------------------------------------------------------------------------------*/
static state_id_t state_idle (event_t event)
{
	switch(event)
	{
	case EVENT_STATE_ENTRY:
		//startup tasks - set things to 0
		//create light monitoring task
//		camera_module_init();
//		network_module_init();
		printf("IDLE STATE START\n");
		return STATE_IDLE;
		break;
	case EVENT_STARTUP_COMPLETE:
		ESP_LOGI(TAG, "Startup complete");
		return STATE_CAMERA_ACTIVE;
	case EVENT_FAULT:
		return STATE_FAULT;
		break;
	case EVENT_DISABLE:
		return STATE_DISABLE;
		break;
	default:
		return STATE_ANY;
		break;
	}
}

static state_id_t state_camera_active (event_t event)
{
	switch(event)
	{
	case EVENT_STATE_ENTRY:
		//startup tasks - set things to 0
		//send startup complete
		xTaskNotifyGive(camera_task);
		printf("CAMERA ACTIVE\n");
		//start task monitor brightness
		//if success
		return STATE_CAMERA_ACTIVE;
		//else
		//return STATE_FAULT;
		break;
	case EVENT_FAULT:
		return STATE_FAULT;
		break;
	case EVENT_DISABLE:
		return STATE_DISABLE;
		break;
	default:
		return STATE_ANY;
		break;
	}
}

static state_id_t state_fault (event_t event)
{
	switch(event)
	{
	case EVENT_STATE_ENTRY:
		//handle
		return EVENT_FAULT;
		break;
	case EVENT_DISABLE:
		return STATE_DISABLE;
		break;
	default:
		return STATE_ANY;
		break;
	}
	return NULL;
}

static state_id_t state_disable (event_t event)
{
	switch(event)
	{
	case EVENT_STATE_ENTRY:
		//destroy ongoing tasks
		//turn off IR LEDs
		return STATE_DISABLE;
		break;
	case EVENT_ENABLE:
		return STATE_IDLE;
		break;
	default:
		return STATE_ANY;
		break;
	}
	return NULL;
}

//static void check_light_lvl_task(void *arg)
//{
//	uint16_t val = adc1_get_raw(LIGHT_SENSE_PIN);
//
//	//TODO: finish this task
//}

// ret_code = adc1_config_width(ADC_WIDTH_BIT_12); //TODO: ADC stuff for later
// if (ret_code != ESP_OK)
// {
// 	printf("adc config fail");
// 	return ret_code;
// }
// adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_DB_0);
// if (ret_code != ESP_OK)
// {
// 	printf("adc config fail");
// 	return ret_code;
// }
