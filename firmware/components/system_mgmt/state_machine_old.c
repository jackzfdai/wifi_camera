/*
 * Data structures and functions to implement system state machine
 *
 * Author: Jack Dai
 * Date: October 18, 2019
 */

/*-------------------------------------------------------------------------------------------------
 * Includes
 --------------------------------------------------------------------------------------------------*/
#include "include/state_machine_old.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bsp.h"
/*-------------------------------------------------------------------------------------------------
 * Defines
 --------------------------------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------------------------------
 * Typedefs
 --------------------------------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------------------------------
 * Private Function Declarations
 --------------------------------------------------------------------------------------------------*/
static esp_err_t hsm_process_event(hsm_handle_t *handle, event_old_t event);
/*-------------------------------------------------------------------------------------------------
 * Public Function Definitions
 --------------------------------------------------------------------------------------------------*/
esp_err_t hsm_init(hsm_init_t *init, hsm_handle_t *handle, hsm_handle_t *super_state_hsm)//, state_old_t *super_state)
{
	esp_err_t ret_code = ESP_FAIL;

	if (init == NULL || handle == NULL || init->STATE_DISABLE == NULL)
	{
		printf("hsm init fail due to null arguments. %p, %p, %p.\n", init, handle, init->STATE_DISABLE);
		return ESP_FAIL;
	}

	if(init->event_queue_buffer == NULL)
	{
		printf("hsm init fail due to null event queue buffer.\n");
		return ESP_FAIL;
	}

//	if((super_state_hsm == NULL)) // != (super_state == NULL))
//	{
//		printf("hsm init fail due to implausible super state arguments.\n");
//		return ESP_FAIL;
//	}

	// if (super_state_hsm != NULL)
	// {
	// 	uint16_t state_position = 256;
	// 	for (uint8_t index = 0; index < super_state_hsm->init.num_states; index ++)
	// 	{
	// 		if (super_state == &super_state_hsm->init.state_table[index])
	// 		{
	// 			state_position = index;
	// 			break;
	// 		}
	// 	}
	// 	if(state_position > 255)
	// 	{
	// 		printf("hsm init fail due to super state not being found in super state machine.\n");
	// 		return ESP_FAIL;
	// 	}
	// }

	//TODO: for loop to check if the super state is actually inside the super hsm

	memcpy(&handle->init, init, sizeof(hsm_init_t));

	handle->state = handle->init.STATE_DISABLE;
	//handle->super_state = super_state;
	handle->super_hsm = super_state_hsm;
//	if (init->max_num_events > init->event_item_size)

//	handle->event_queue_handle = xQueueCreate(init->max_num_events, // The number of items the queue can hold.
//	                         sizeof(event_old_t));

	handle->event_queue_handle = xQueueCreateStatic(init->max_num_events, // The number of items the queue can hold.
	                         sizeof(event_old_t),     // The size of each item in the queue
	                         handle->init.event_queue_buffer, // The buffer that will hold the items in the queue.
	                         &handle->init.event_queue); //buffer to hold queue structure



	if (handle->super_hsm == NULL) // state machine is the super-most state machine, enable it
	{
		printf("hsm init ok, sending event %p\n", handle->init.event_queue_buffer);
		ret_code = hsm_send_evt(handle, handle->init.EVENT_ENABLE, portMAX_DELAY);
	}
	else //state machine is nested inside a super state, leave enabling it to the super state
	{
		ret_code = ESP_OK;
	}

	return ret_code;
}

void hsm_task(hsm_handle_t *handle)
{
	static uint32_t count = 0;
	event_old_t event;
	while(1)
	{
		count++;
//		printf("handle send %p\n", handle->event_queue_handle);
		if (handle->event_queue_handle == NULL)
				printf("task null queue handle");
		else if (xQueueReceive(handle->event_queue_handle, &event, (TickType_t )(100/portTICK_PERIOD_MS)) != pdTRUE) //0
		{
//			printf("nothing from queue\n");
			continue;
		}
//		else
//		{
////			printf("event received %d\n", event);
//		}
		if (hsm_process_event(handle, event) != ESP_OK)
		{
			printf("failed to process event %d\n", event);
		}
		//printf("hsm task ct %d\n", count);
		vTaskDelay(100/portTICK_PERIOD_MS);//pdMS_TO_TICKS(500));
	}
}

state_id_t hsm_get_state(hsm_handle_t *handle)
{
	return handle->state->id;
}

esp_err_t hsm_send_evt(hsm_handle_t *handle, event_old_t event, uint32_t timeout_ms)
{
//	printf("handle send in hsm send evt%p\n", handle->event_queue_handle);
	if (handle->event_queue_handle == NULL)
	{
		printf("send null hsm evt send\n");
		return ESP_FAIL;
	}
	if (xQueueSend(handle->event_queue_handle, (void *) &event, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
	{
		printf("send fail hsm evt send\n");
		return ESP_FAIL;
	}

//	printf("hsm send evt success event %d\n", event);
	return ESP_OK;
}

esp_err_t hsm_send_evt_urgent(hsm_handle_t *handle, event_old_t event, uint32_t timeout_ms)
{
	printf("bp 1.1\n");
	if (handle->event_queue_handle == NULL)
			return ESP_FAIL;

	printf("bp 1.1\n");
	if (xQueueSendToFront(handle->event_queue_handle, &event, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
	{

		return ESP_FAIL;
	}

	return ESP_OK;
}

esp_err_t hsm_send_evt_isr(hsm_handle_t *handle, event_old_t event)
{
	if (handle->event_queue_handle == NULL)
			return ESP_FAIL;

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	if(xQueueSendFromISR(handle->event_queue_handle, &event, &xHigherPriorityTaskWoken) != pdTRUE)
	{
		return ESP_FAIL;
	}

	portYIELD_FROM_ISR();
	return ESP_OK;
}

esp_err_t hsm_send_evt_urgent_isr(hsm_handle_t *handle, event_old_t event)
{
	if (handle->event_queue_handle == NULL)
			return ESP_FAIL;

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	if(xQueueSendToFrontFromISR(handle->event_queue_handle, &event, &xHigherPriorityTaskWoken) != pdTRUE)
	{
		return ESP_FAIL;
	}

	portYIELD_FROM_ISR();
	return ESP_OK;
}

uint8_t hsm_state_table_find_state(state_id_t id, state_old_t *state_table)
{
	assert(state_table != NULL);

	uint8_t state_cnt = sizeof(state_table)/sizeof(state_table[0]);

	for (uint8_t index = 0; index < state_cnt; index ++)
	{
		if (state_table[index].id == id)
			return index;
	}

	return 255;
}
/*-------------------------------------------------------------------------------------------------
 * Private Function Definitions
 --------------------------------------------------------------------------------------------------*/
static esp_err_t hsm_process_event(hsm_handle_t *handle, event_old_t event)
{
	esp_err_t ret_code;

//	state_old_t * new_state;
	state_id_t new_state_id = handle->init.STATE_ID_ANY;
//	transition_old_t *transitions = handle->init.transitions;

	//printf("bp 1\n");

	if (event == WATCHDOG_RQST_EVT) //not currently used, in case need custom watchdog
	{
		//TODO: if required
		ret_code = ESP_OK;
		return ret_code;
	}

//	printf("bp 2\n");

	if (event > handle->init.max_num_events)
	{
		//TODO: figure out error prints
		ret_code = ESP_FAIL;
		return ret_code;
	}

//	new_state = NULL;
//	new_state = handle->state->fn(event);
//	printf("bp 3\n");

	new_state_id = handle->state->fn(event);

	if (new_state_id == handle->init.STATE_ID_ANY)
	{
//		if (handle->super_state != NULL)
//		{
//			ret_code = hsm_send_evt(handle->super_hsm, event, HSM_TIMEOUT_MS);
//			return ret_code;
//		}
		ret_code = ESP_OK;
		return ret_code;

	}

//	printf("bp 4\n");

	if (new_state_id != handle->state->id)
	{
//		printf("bp 5\n");

//		if(handle->state->nested_hsm != NULL)
//		{
//			ESP_ERROR_CHECK(hsm_send_evt_isr(handle->state->nested_hsm, handle->state->nested_hsm->init.EVENT_DISABLE));
//		}

		//get the new state based on new state id
		uint8_t index = 0;
		for (index = 0; index < handle->init.num_states; index ++)
		{
			if (new_state_id == handle->init.state_table[index].id)
			{
				handle->state = &handle->init.state_table[index];
				break;
			}
		}

		//if new state w/ state id not found in state table
		if (index >= handle->init.num_states)
		{
			printf("failed to recognize state id. Current state id: %d, new state id: %d\n", handle->state->id, new_state_id);
			ret_code = ESP_FAIL;
			return ret_code;
		}

//		printf("bp 6\n");

//		handle->state = new_state;

		//enable any nested states of the new state //leave this to individual state handlers to allow multiple nesting
		// if(handle->state->nested_hsm != NULL)
		// {
		// 	ESP_ERROR_CHECK(hsm_send_evt_urgent(handle->state->nested_hsm, handle->state->nested_hsm->init.EVENT_ENABLE, portMAX_DELAY));
		// }

		ESP_ERROR_CHECK(hsm_send_evt_urgent(handle, handle->init.EVENT_STATE_ENTRY, portMAX_DELAY));

//		printf("bp 7\n");

		ret_code = ESP_OK;
	}
	else
	{
//		printf("bp 8\n");

		//send watchdog evt? (for in case need custom watchdog)
		ret_code = ESP_OK;
	}

	return ret_code;
}
