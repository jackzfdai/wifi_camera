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

#include "esp_camera.h" //temp

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


/*---------private functions----------*/
static void network_data_send_task(void *pvParameter);
//static void network_cmd_send_task();
//static void network_rcv_task();

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

/*------------------------------------*/

esp_err_t network_module_init()
{
	esp_err_t ret_val = ESP_OK;

	network_rcv_queue = xQueueCreateStatic(NETWORK_RCV_QUEUE_LENGTH, sizeof(network_module_cmds_t), (uint8_t*) network_rcv_buffer, &network_rcv_queue_data);
	network_cmd_send_queue = xQueueCreateStatic(NETWORK_CMD_SEND_QUEUE_LENGTH, sizeof(network_module_cmds_t), (uint8_t*) network_cmd_send_buffer, &network_cmd_send_queue_data);
	network_data_send_queue = xQueueCreateStatic(NETWORK_DATA_SEND_QUEUE_LENGTH, sizeof(network_module_generic_data_t), (uint8_t*) network_data_send_buffer, &network_data_send_queue_data);

	if(network_rcv_queue == NULL || network_cmd_send_queue == NULL || network_data_send_queue == NULL)
	{
		ret_val = ESP_FAIL;
		return ret_val;
	}

	wifi_init_sta();
//	ESP_LOGI(TAG, "free heaps after wifi init: %d\n", xPortGetFreeHeapSize());

	protocol_init_t protocol_init;
	protocol_init.evt_handler = NULL;
	ret_val = protocol_session_init(&protocol_init);

	xTaskCreatePinnedToCore(network_data_send_task,"network_data_send_task",2048,NULL,NETWORK_DATA_SEND_PRIO, NULL, 1);
	//xTaskCreatePinnedToCore(network_cmd_send_task,"network_cmd_send_task",1024,NULL,NETWORK_CMD_SEND_PRIO, NULL, 0);
	//xTaskCreatePinnedToCore(network_rcv_task,"network_rcv_task",1024,NULL,NETWORK_RCV_PRIO, NULL, 0);

	return ret_val;
}

static void network_data_send_task(void *pvParameter)
{
	static TickType_t frame_tick = 0;
	static const char *payload = "Message from ESP32 ";

	while(1)
	{
		network_module_generic_data_t data;
		xQueueReceive(network_data_send_queue, (void *) &data, portMAX_DELAY);

        TickType_t temp = xTaskGetTickCount();
        frame_tick = temp;

        while(!network_ready)
        {
			vTaskDelay(200/portTICK_PERIOD_MS);
			ESP_LOGI(TAG, "Waiting on network ready.");
        }

        	TickType_t frame_send_time = xTaskGetTickCount();
            protocol_send_data(data.buf, data.size);
            frame_send_time = xTaskGetTickCount() - frame_send_time;

        ESP_LOGI(TAG, "free DMA-capable heap size: %d, frame send time %d0 ms", heap_caps_get_minimum_free_size(MALLOC_CAP_DMA), frame_send_time);

        //TODO: implement a back off depending on memory availability
//		vTaskDelay(50/portTICK_PERIOD_MS);

		xTaskNotifyGive(camera_task);
	}
}

//static void network_cmd_send_task(void *pvParameter)
//{
//	while(1)
//	{
//
//	}
//}
//
//static void network_rcv_task(void *pvParameter)
//{
//	while(1)
//	{
//
//	}
//}

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
