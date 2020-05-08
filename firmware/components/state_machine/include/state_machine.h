#ifndef STATE_MACH_H
#define STATE_MACH_H

/*-------------------------------------------------------------------------------------------------
 * Includes
 --------------------------------------------------------------------------------------------------*/
#include <stdlib.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*-------------------------------------------------------------------------------------------------
 * Defines
 --------------------------------------------------------------------------------------------------*/
#define FSM_TIMEOUT_MS		(pdMS_TO_TICKS(15))
#define TRANSITION_COUNT(transition_table) \
	((sizeof(transition_table))/(sizeof(*transition_table)))
/*-------------------------------------------------------------------------------------------------
 * Typedefs
 --------------------------------------------------------------------------------------------------*/
typedef uint32_t state_t;
typedef uint32_t event_t;

typedef struct
{
	state_t curr_state;
	event_t event;
	state_t new_state;
	void (*transition_fn)(void); //add error handling?
} transition_t;

//struct to hold init properties of fsm
typedef struct
{
	const char * FSM_LOG_TAG;

	StaticQueue_t event_queue_data;
	event_t *event_queue_buffer;
	uint32_t event_queue_len;

	state_t STATE_DEFAULT; //default state when first initialized
	state_t STATE_ID_ANY; //used when current state has no action for the event/event is unknown

	transition_t * transition_table;
	uint32_t transition_table_size;

	BaseType_t task_core;
} fsm_init_t;

typedef struct
{
	state_t curr_state;
	QueueHandle_t event_queue_handle;
	fsm_init_t init;
} fsm_handle_t;

/*-------------------------------------------------------------------------------------------------
 * Function Declarations
 --------------------------------------------------------------------------------------------------*/
esp_err_t fsm_init(fsm_init_t *init, fsm_handle_t *handle); //, state_t *super_state);

state_t fsm_get_state(fsm_handle_t *fsm);

esp_err_t fsm_send_evt(fsm_handle_t *fsm, event_t event, uint32_t timeout_ms);

esp_err_t fsm_send_evt_urgent(fsm_handle_t *fsm, event_t event, uint32_t timeout_ms);

esp_err_t fsm_send_evt_isr(fsm_handle_t *fsm, event_t event);

esp_err_t fsm_send_evt_urgent_isr(fsm_handle_t *fsm, event_t event);

//uint8_t fsm_state_table_find_state(state_id_t id, state_t *state_table);


#endif
