#include "state_machine.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

/*-------------------------------------------------------------------------------------------------
 * Defines
 --------------------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------------------
 * Typedefs
 --------------------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------------------
 * Private Variable Definitions
 --------------------------------------------------------------------------------------------------*/
static const char * GENERIC_LOG_TAG = "GENERIC FSM";

/*-------------------------------------------------------------------------------------------------
 * Private Function Declarations
 --------------------------------------------------------------------------------------------------*/
static esp_err_t fsm_process_event(fsm_handle_t *fsm, event_t event);

/*-------------------------------------------------------------------------------------------------
 * Public Function Definitions
 --------------------------------------------------------------------------------------------------*/
esp_err_t fsm_init(fsm_init_t *init, fsm_handle_t *fsm)
{
	if (init == NULL || fsm == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}

	memcpy(&fsm->init, init, sizeof(fsm_init_t));

	fsm->event_queue_handle = xQueueCreateStatic(fsm->init.event_queue_size, // The number of items the queue can hold.
	                         sizeof(event_t),     // The size of each item in the queue
	                         (uint8_t*) fsm->init.event_queue_buffer, // The buffer that will hold the items in the queue.
	                         &fsm->init.event_queue); //buffer to hold queue structure

	if (fsm->event_queue_handle == NULL)
	{
		return ESP_ERR_INVALID_SIZE;
	}

	fsm->curr_state = fsm->init.STATE_DEFAULT;

	return ESP_OK;
}

void fsm_task(fsm_handle_t *fsm)
{
	event_t event;
	while(1)
	{
		xQueueReceive(fsm->event_queue_handle, &event, portMAX_DELAY);

		if (fsm_process_event(fsm, event) != ESP_OK)
		{
			ESP_LOGE(fsm->init.FSM_LOG_TAG, "Failed to process event.");
		}
	}
}

state_t fsm_get_state(fsm_handle_t *fsm)
{
	return fsm->curr_state;
}

esp_err_t fsm_send_evt(fsm_handle_t *fsm, event_t event, uint32_t timeout_ms)
{
	if (fsm == NULL || fsm->event_queue_handle == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}
	if (xQueueSend(fsm->event_queue_handle, (void *) &event, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
	{
		ESP_LOGE(fsm->init.FSM_LOG_TAG, "Failed to send event to queue.");
		return ESP_FAIL;
	}
	return ESP_OK;
}

esp_err_t fsm_send_evt_urgent(fsm_handle_t *fsm, event_t event, uint32_t timeout_ms)
{
	if (fsm == NULL || fsm->event_queue_handle == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}

	if (xQueueSendToFront(fsm->event_queue_handle, &event, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
	{
		ESP_LOGE(fsm->init.FSM_LOG_TAG, "Failed to send event to front of queue.");
		return ESP_FAIL;
	}
	return ESP_OK;
}

esp_err_t fsm_send_evt_isr(fsm_handle_t *fsm, event_t event)
{
	if (fsm == NULL || fsm->event_queue_handle == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	if(xQueueSendFromISR(fsm->event_queue_handle, &event, &xHigherPriorityTaskWoken) != pdTRUE)
	{
		return ESP_FAIL;
	}

	if (xHigherPriorityTaskWoken)
		portYIELD_FROM_ISR();

	return ESP_OK;
}

esp_err_t fsm_send_evt_urgent_isr(fsm_handle_t *fsm, event_t event)
{
	if (fsm == NULL || fsm->event_queue_handle == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	if(xQueueSendToFrontFromISR(fsm->event_queue_handle, &event, &xHigherPriorityTaskWoken) != pdTRUE)
	{
		return ESP_FAIL;
	}

	if (xHigherPriorityTaskWoken)
		portYIELD_FROM_ISR();

	return ESP_OK;
}

/*-------------------------------------------------------------------------------------------------
 * Private Function Definitions
 --------------------------------------------------------------------------------------------------*/
static esp_err_t fsm_process_event(fsm_handle_t *fsm, event_t event)
{
	if (fsm == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}

	uint32_t index = 0;
	for (index = 0; index < fsm->init.transition_table_size; index ++)
	{
		if (fsm->init.transition_table[index].curr_state == fsm->curr_state && fsm->init.transition_table[index].event == event)
		{
			fsm->curr_state = fsm->init.transition_table[index].new_state;
			fsm->init.transition_table[index].transition_fn();
			break;
		}
	}

	if (index == fsm->init.transition_table_size)
	{
		ESP_LOGW(fsm->init.FSM_LOG_TAG, "No defined state transition for received event.");
	}

	return ESP_OK;
}
