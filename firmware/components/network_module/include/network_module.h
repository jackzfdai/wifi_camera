#ifndef NETWORK_MODULE_H
#define NETWORK_MODULE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"

#define NETWORK_MODULE_BASE			0x1000

#define NETWORK_DATA_SEND_QUEUE_LENGTH	8
#define NETWORK_CMD_SEND_QUEUE_LENGTH	8
#define NETWORK_RCV_QUEUE_LENGTH	8

#define NETWORK_DATA_SEND_PRIO	5
#define NETWORK_CMD_SEND_PRIO	1
#define NETWORK_RCV_PRIO	1

typedef enum
{
	NETWORK_START_STREAM = NETWORK_MODULE_BASE,
	NETWORK_STOP_STREAM
} network_module_cmds_t;

typedef enum
{
	STATE_NOT_CONNECTED = NETWORK_MODULE_BASE,
	STATE_CONNECTING,
	STATE_CONNECTED,
	STATE_STREAMING,
	STATE_GENERIC
} network_module_state_t;

typedef enum
{
	EVENT_SESSION_RQST = NETWORK_MODULE_BASE,
	EVENT_SESSION_RQST_TIMEOUT,
	EVENT_SESSION_CONNECTED,
	EVENT_SESSION_END,
	EVENT_SESSION_KEEPALIVE,
	EVENT_SESSION_TIMEOUT,
	EVENT_STREAM_START_RQST,
	EVENT_STREAM_STOP_RQST,
	EVENT_ERROR
} network_module_event_t;

typedef struct
{
	void * buf;
	uint32_t size;
} network_module_generic_data_t;

extern QueueHandle_t network_data_send_queue;

esp_err_t network_module_init();

#endif

