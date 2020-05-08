#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

#include "network_module.h"
#include "system_mgmt.h"
#include "camera_module.h"

//#include "esp_camera.h" //temp

#include "tcpip_adapter.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "nvs_flash.h"

#include "esp_log.h"

#include "protocol.h"
#include "state_machine.h"

#define NETWORK_FSM_QUEUE_LEN		10

/*---------private functions----------*/
static void network_data_send_task(void *pvParameter);
//static void network_cmd_send_task();
static void network_rcv_task();

static void network_ctrl_task(void * parameters);

static void protocol_send_session_rqst_response(void);
static void protocol_session_start(void);
static void protocol_session_keepalive(void);
static void protocol_session_end(void);
static void protocol_start_stream(void);
static void protocol_end_stream(void);

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wifi_init_sta(void);

/*---------private variables----------*/
volatile bool network_ready = false;
static const char* TAG = "network_application";

static QueueHandle_t network_rcv_queue;
static StaticQueue_t network_rcv_queue_data;
static network_module_cmds_t network_rcv_buffer[NETWORK_RCV_QUEUE_LENGTH];

static QueueHandle_t network_cmd_send_queue;
static StaticQueue_t network_cmd_send_queue_data;
static network_module_cmds_t network_cmd_send_buffer[NETWORK_CMD_SEND_QUEUE_LENGTH];

QueueHandle_t network_data_send_queue;
StaticQueue_t network_data_send_queue_data;
network_module_generic_data_t network_data_send_buffer[NETWORK_DATA_SEND_QUEUE_LENGTH];

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about one event
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG_WIFI_STATION = "wifi station";

static int s_retry_num = 0;



//static void network_module_system_up(void);

transition_t protocol_transitions[] =
		{
				{STATE_START_UP, EVENT_SYSTEM_UP, STATE_SESSION_NOT_CONNECTED, NULL},
				{STATE_SESSION_NOT_CONNECTED, EVENT_SESSION_RQST, STATE_SESSION_CONNECTING, &protocol_send_session_rqst_response},
				{STATE_SESSION_CONNECTING, EVENT_SESSION_RQST_TIMEOUT, STATE_SESSION_NOT_CONNECTED, NULL},
				{STATE_SESSION_CONNECTING, EVENT_SESSION_KEEPALIVE, STATE_SESSION_CONNECTED, &protocol_session_start},
				{STATE_SESSION_CONNECTED, EVENT_SESSION_KEEPALIVE, STATE_SESSION_CONNECTED, &protocol_session_keepalive},
				{STATE_SESSION_CONNECTED, EVENT_SESSION_TIMEOUT, STATE_SESSION_NOT_CONNECTED, &protocol_session_end},
				{STATE_SESSION_CONNECTED, EVENT_SESSION_END, STATE_SESSION_NOT_CONNECTED, &protocol_session_end},
				{STATE_SESSION_CONNECTED, EVENT_STREAM_START_RQST, STATE_SESSION_STREAMING, &protocol_start_stream},
				{STATE_SESSION_STREAMING, EVENT_STREAM_STOP_RQST, STATE_SESSION_CONNECTED, &protocol_end_stream},
				{STATE_SESSION_STREAMING, EVENT_ERROR, STATE_SESSION_CONNECTED, &protocol_end_stream},
				{STATE_SESSION_STREAMING, EVENT_SESSION_TIMEOUT, STATE_SESSION_NOT_CONNECTED, &protocol_session_end},
				{STATE_SESSION_STREAMING, EVENT_SESSION_END, STATE_SESSION_NOT_CONNECTED, &protocol_session_end}
		}; //TODO: add state handling for if wifi is disconnected

static StaticQueue_t network_fsm_queue_data;
static event_t network_fsm_queue_buffer[NETWORK_FSM_QUEUE_LEN];

static fsm_handle_t network_fsm;

/*------------------------------------*/

esp_err_t network_module_init()
{
	esp_err_t ret_val = ESP_OK;

//	network_fsm_queue = xQueueCreateStatic(NETWORK_FSM_QUEUE_LEN, sizeof(event_t), (uint8_t*) network_fsm_queue_buffer, &network_fsm_queue_data);
	fsm_init_t network_fsm_init;
	network_fsm_init.FSM_LOG_TAG = TAG;
	network_fsm_init.STATE_DEFAULT = STATE_START_UP;
	network_fsm_init.STATE_ID_ANY = STATE_GENERIC;
	network_fsm_init.event_queue_data = &network_fsm_queue_data;
	network_fsm_init.event_queue_buffer = network_fsm_queue_buffer;
	network_fsm_init.event_queue_len = NETWORK_FSM_QUEUE_LEN;
	network_fsm_init.transition_table = protocol_transitions;
	network_fsm_init.transition_table_size = TRANSITION_COUNT(protocol_transitions);
	network_fsm_init.task_core = 0;

	ret_val = fsm_init(&network_fsm_init, &network_fsm);
	if (ret_val != ESP_OK)
	{
		ESP_LOGE(TAG, "Network state machine init failed.");
		return ret_val;
	}

	network_rcv_queue = xQueueCreateStatic(NETWORK_RCV_QUEUE_LENGTH, sizeof(network_module_cmds_t), (uint8_t*) network_rcv_buffer, &network_rcv_queue_data);
	network_cmd_send_queue = xQueueCreateStatic(NETWORK_CMD_SEND_QUEUE_LENGTH, sizeof(network_module_cmds_t), (uint8_t*) network_cmd_send_buffer, &network_cmd_send_queue_data);
	network_data_send_queue = xQueueCreateStatic(NETWORK_DATA_SEND_QUEUE_LENGTH, sizeof(network_module_generic_data_t), (uint8_t*) network_data_send_buffer, &network_data_send_queue_data);

	if(network_rcv_queue == NULL || network_cmd_send_queue == NULL || network_data_send_queue == NULL)
	{
		ret_val = ESP_FAIL;
		return ret_val;
	}

	wifi_init_sta();

	protocol_init_t protocol_init;
	protocol_init.evt_handler = NULL;
	ret_val = protocol_session_init(&protocol_init);

	xTaskCreatePinnedToCore(network_data_send_task,"network_data_send_task",2048,NULL,NETWORK_DATA_SEND_PRIO, NULL, 0);
	//xTaskCreatePinnedToCore(network_cmd_send_task,"network_cmd_send_task",1024,NULL,NETWORK_CMD_SEND_PRIO, NULL, 0);
	xTaskCreatePinnedToCore(network_rcv_task,"network_rcv_task",1024,NULL,NETWORK_RCV_PRIO, NULL, 0);

	return ret_val;
}



static void network_ctrl_task (void * parameter)
{
	while(1)
	{
		switch(network_fsm.curr_state)
		{
		case STATE_SESSION_NOT_CONNECTED:
			break;
		case STATE_SESSION_CONNECTING:
			break;
		case STATE_SESSION_CONNECTED:
			break;
		case STATE_SESSION_STREAMING:
			break;
		default:
			break;
		}
	}
}

static void network_data_send_task(void *pvParameter)
{
	while(1)
	{
		if (network_fsm.curr_state == STATE_START_UP)
		{
			vTaskDelay(200/portTICK_PERIOD_MS);
			continue;
		}
		void * buf = NULL;
		uint32_t size = 0;
		esp_err_t ret_val = camera_get_jpeg(&buf, &size, portMAX_DELAY);
		if (ret_val != ESP_OK)
		{
			ESP_LOGE(TAG, "Frame get error.");
			continue;
		}

		TickType_t frame_send_time = xTaskGetTickCount();
		protocol_send_data(buf, size);
		frame_send_time = xTaskGetTickCount() - frame_send_time;

		ESP_LOGI(TAG, "free DMA-capable heap size: %d, frame send time %d0 ms", heap_caps_get_minimum_free_size(MALLOC_CAP_DMA), frame_send_time);

		//TODO: implement a back off depending on memory availability
//		vTaskDelay(50/portTICK_PERIOD_MS);
		ret_val = camera_return_jpeg(buf);
		if (ret_val != ESP_OK)
		{
			ESP_LOGE(TAG, "Frame return error.");
			continue;
		}
	}
}

void protocol_evt_handler(protocol_evt_t evt)
{
	switch(evt)
	{
	case PROTOCOL_EVT_SESSION_STARTED:
		//permit data sending
		break;
	case PROTOCOL_EVT_SESSION_ENDED:
		break;
	case PROTOCOL_EVT_STREAM_RQST:
		break;
	case PROTOCOL_EVT_STREAM_STOP:
		break;
	default:
		break;
	}
}

//static void network_module_system_up(void)
//{
//    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
//    if (err != 0) {
//        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
//    }
//}

static void protocol_send_session_rqst_response(void)
{
	return;
}

static void protocol_session_start(void)
{
	return;
}

static void protocol_session_keepalive(void)
{
	return;
}

static void protocol_session_end(void)
{
	return;
}

static void protocol_start_stream(void)
{
	return;
}

static void protocol_end_stream(void)
{
	return;
}

//static void network_session_ctrl_task (void *pvParameter)
//{
//	while (!(xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT))
//	{
//		vTaskDelay(500/portTICK_PERIOD_MS);
//	}
//
//	while (1)
//	{
//
//		protocol_init_t protocol_init;
//		protocol_init.evt_handler = NULL;
//		protocol_session_init(&protocol_init);
//	}
//}

//static void network_cmd_send_task(void *pvParameter)
//{
//	while(1)
//	{
//
//	}
//}
//
static void network_rcv_task(void *pvParameter)
{
	while(1)
	{
		//receive frm network
		while (network_fsm.curr_state == STATE_START_UP)
		{
			vTaskDelay(200/portTICK_PERIOD_MS);
		}

		uint8_t * recv_buf;
		int len = protocol_recv_ctrl(&recv_buf);
	}
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT)
	{
		switch(event_id)
		{
		case WIFI_EVENT_STA_START:
	        esp_wifi_connect();
	        ESP_LOGI(TAG_WIFI_STATION, "wifi start");
	        break;
		case WIFI_EVENT_STA_CONNECTED:
			ESP_LOGI(TAG_WIFI_STATION, "wifi station connected");
			break;
		case WIFI_EVENT_STA_DISCONNECTED:
	        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY)
	        {
	            esp_wifi_connect();
	            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	            s_retry_num++;
	            ESP_LOGI(TAG_WIFI_STATION, "retry to connect to the AP");
	        }
	        ESP_LOGI(TAG_WIFI_STATION,"connect to the AP fail");
	        break;
		default:
			break;
		}
	}
	else if (event_base == IP_EVENT)
	{
		switch (event_id)
		{
			case IP_EVENT_STA_GOT_IP:
			{
				network_ready = true;
				ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
				ESP_LOGI(TAG_WIFI_STATION, "got ip:%s",
						 ip4addr_ntoa(&event->ip_info.ip));
				s_retry_num = 0;
				xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
				fsm_send_evt(&network_fsm, EVENT_SYSTEM_UP, 0);

				break;
			}
			default:
				break;
		}
	}
}

static void wifi_init_sta(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    tcpip_adapter_init();

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );
//    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
//    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
//             CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
}
