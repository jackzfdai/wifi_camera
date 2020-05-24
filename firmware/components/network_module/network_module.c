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
#define NETWORK_SESSION_TIMEOUT_US	(5000000U)

/*------------typedefs-------------------*/
typedef struct
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;
    struct sockaddr_in local;
	struct sockaddr_in remote;
    int sock;
} udp_server_s; //udp server data

typedef struct
{
	udp_server_s server;
	uint8_t current_frame_id;
	uint8_t * frame_buf;
} m_protocol_ctrl; //protocol session data

/*-----------------------------private functions------------------------------*/
/*-----------Tasks-----------*/
static void network_data_send_task(void *pvParameter);
static void network_rcv_task(void *parameters);

/*---------UDP server--------*/
static esp_err_t udp_server_init(void);
static void udp_server_destroy(void);

/*-------Protocol-mgmt-------*/
static esp_err_t protocol_send_data(void * buf, uint32_t len);
int protocol_recv_ctrl(void** buf, struct sockaddr_in * source_addr);
static void process_network_rcv(uint8_t * packet, int len, struct sockaddr_in * source);
static void session_timeout_cb(void* arg);
static void session_keepalive(void);
static void session_keep_alive_stop(void);
//static void session_timer_start(void);

/*-------Wifi interface------*/
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wifi_init_sta(void);

/*---------------------------private variables--------------------------------*/
static const char* TAG = "network_application";

/*------------Wifi module interface--------*/
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about one event
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG_WIFI_STATION = "wifi station";
static int s_retry_num = 0;
//static void network_module_system_up(void);

/*-------protocol session mgmt----------*/
static fsm_handle_t network_fsm; //protocol state machine
static StaticQueue_t network_fsm_queue_data;
static event_t network_fsm_queue_buffer[NETWORK_FSM_QUEUE_LEN];

esp_timer_handle_t session_timeout; //protocol session timer

transition_t protocol_transitions[] = //protocol state transition table
		{
				{STATE_START_UP, EVENT_SYSTEM_UP, STATE_SESSION_IDLE, NULL},
				{STATE_SESSION_IDLE, EVENT_STREAM_START_RQST, STATE_SESSION_STREAMING, session_keepalive},
				{STATE_SESSION_STREAMING, EVENT_STREAM_STOP_RQST, STATE_SESSION_IDLE, session_keep_alive_stop},
				{STATE_SESSION_STREAMING, EVENT_ERROR, STATE_SESSION_IDLE, NULL},
				{STATE_SESSION_STREAMING, EVENT_STREAM_KEEPALIVE, STATE_SESSION_STREAMING, session_keepalive},
				{STATE_SESSION_STREAMING, EVENT_SESSION_TIMEOUT, STATE_SESSION_IDLE, NULL},
				{STATE_GENERIC, EVENT_WIFI_DISCONNECTED, STATE_START_UP, NULL}
		}; //TODO: add state handling for if wifi is disconnected

static m_protocol_ctrl session; //protocol session data
static uint8_t protocol_frame_buf[PROTOCOL_FRAME_SIZE];

SemaphoreHandle_t session_data_mutx = NULL;
StaticSemaphore_t session_data_mutx_buf;

SemaphoreHandle_t socket_mutx = NULL;
StaticSemaphore_t socket_mutx_buf;
/*------------------------------------*/

esp_err_t network_module_init()
{
	esp_err_t ret_val = ESP_OK;

	//initialize protocol state machine
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

	//initialize protocol session data
	if (sizeof(protocol_packet_hdr_t) > PROTOCOL_FRAME_SIZE)
	{
		ret_val = ESP_ERR_INVALID_SIZE;
		return ret_val;
	}

	session_data_mutx = xSemaphoreCreateMutexStatic(&session_data_mutx_buf);
    socket_mutx = xSemaphoreCreateMutexStatic(&socket_mutx_buf);
    if (session_data_mutx == NULL || socket_mutx == NULL )
    {
    	ret_val = ESP_FAIL;
    	return ret_val;
    }

	session.current_frame_id = 0;
	session.frame_buf = protocol_frame_buf;

	//initialize wifi stack
	wifi_init_sta();

	//initialize UDP server and socket
	ret_val = udp_server_init();
	if (ret_val != ESP_OK)
	{
		return ret_val;
	}

	//create network module tasks
	xTaskCreatePinnedToCore(network_data_send_task,"network_data_send_task",2048,NULL,NETWORK_DATA_SEND_PRIO, NULL, 0);
	xTaskCreatePinnedToCore(network_rcv_task,"network_rcv_task",2048,NULL,NETWORK_RCV_PRIO, NULL, 0);

	//initialize protocol session timer
	esp_timer_create_args_t timer_init;
	timer_init.arg = NULL;
	timer_init.callback = session_timeout_cb;
	timer_init.dispatch_method = ESP_TIMER_TASK;
	timer_init.name = "Session Timeout";

	ret_val = esp_timer_create(&timer_init, &session_timeout);

	return ret_val;
}

static void network_data_send_task(void *pvParameter)
{
	while(1)
	{
//		if (network_fsm.curr_state == STATE_START_UP)
//		{
//			vTaskDelay(200/portTICK_PERIOD_MS);
//			continue;
//		}
		while (network_fsm.curr_state != STATE_SESSION_STREAMING)
		{
			vTaskDelay(200/portTICK_PERIOD_MS);
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

//		ESP_LOGI(TAG, "free DMA-capable heap size: %d, frame send time %d0 ms", heap_caps_get_minimum_free_size(MALLOC_CAP_DMA), frame_send_time);

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

static void network_rcv_task(void *parameters)
{
	while(1)
	{
		//receive frm network
		while (network_fsm.curr_state == STATE_START_UP)
		{
			vTaskDelay(200/portTICK_PERIOD_MS);
		}

		uint8_t * recv_buf = NULL;
		int len = -1;
		struct sockaddr_in src;
		len = protocol_recv_ctrl((void *) &recv_buf, &src);

		if (len > 0)
		{
			process_network_rcv(recv_buf, len, &src);
		}

		vTaskDelay(500/portTICK_PERIOD_MS);
	}
}

static esp_err_t udp_server_init(void)
{
	esp_err_t ret_val = ESP_OK;

    session.server.local.sin_addr.s_addr = htonl(INADDR_ANY);
    session.server.local.sin_family = AF_INET;
    session.server.local.sin_port = htons(CONFIG_DEST_PORT); //TODO: rename this
    session.server.addr_family = AF_INET;
    session.server.ip_protocol = IPPROTO_IP;
    inet_ntoa_r(session.server.local.sin_addr, session.server.addr_str, sizeof(session.server.addr_str) - 1);

    session.server.sock = socket(session.server.addr_family, SOCK_DGRAM, session.server.ip_protocol);
    if (session.server.sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        ret_val = ESP_FAIL;
        return ret_val;
    }
    ESP_LOGI(TAG, "Socket created");

    fcntl(session.server.sock, F_SETFL, O_NONBLOCK);

    int err = bind(session.server.sock, (struct sockaddr *)&session.server.local, sizeof(session.server.local));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ret_val = ESP_FAIL;
        return ret_val;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", CONFIG_DEST_PORT);

	return ret_val;
}

static esp_err_t protocol_send_data(void * buf, uint32_t len)
{
	if (buf == NULL || len == 0)
		return ESP_ERR_INVALID_ARG;

	if (xSemaphoreTake(session_data_mutx, 0) != pdTRUE)
	{
		ESP_LOGW(TAG, "Can't get mutx");
		return ESP_ERR_INVALID_STATE;
	}

	protocol_packet_hdr_t packet;

	packet.frame_id = session.current_frame_id;
	packet.frame_type = PROTOCOL_DATA_PKT;
	packet.pkt_sequence = 1;
	packet.total_packets = (len - 1)/PROTOCOL_MAX_PAYLOAD_SIZE + 1;
	packet.local_timestamp_ms = esp_timer_get_time() / 1000;

	uint32_t bytes_remaining = len;
	uint32_t payload_data_index = 0;

	esp_err_t ret_val = ESP_OK;

	uint8_t * buf_8b = (uint8_t *) buf;

	for (uint32_t pkt_num = 0; pkt_num < packet.total_packets; pkt_num ++)
	{
		if (bytes_remaining > PROTOCOL_MAX_PAYLOAD_SIZE)
			packet.payload_len = PROTOCOL_MAX_PAYLOAD_SIZE;
		else
			packet.payload_len = bytes_remaining;

		memcpy((void *) session.frame_buf, (void *) packet.val, PROTOCOL_HEADER_SIZE);
		memcpy((void *) &session.frame_buf[PROTOCOL_HEADER_SIZE], (void *) &buf_8b[payload_data_index], packet.payload_len);

//		ret_val = session_udp_client_send(/*payload*/session.frame_buf, /*size*/packet.payload_len + PROTOCOL_HEADER_SIZE);

		if (xSemaphoreTake(socket_mutx, portMAX_DELAY) != pdTRUE)
		{
			ESP_LOGE(TAG, "Can't get socket mutx, shouldn't be here");
			ret_val = ESP_FAIL;
		}
		else
		{
			int err = sendto(session.server.sock, session.frame_buf, packet.payload_len + PROTOCOL_HEADER_SIZE, 0, (struct sockaddr *)&session.server.remote, sizeof(session.server.remote));
			if (err < 0) {
				ret_val = ESP_FAIL;
				ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
			}
			else
			{
				ESP_LOGI(TAG, "Message sent");
			}
		}

		xSemaphoreGive(socket_mutx);

		if (ret_val != ESP_OK)
			break;

		payload_data_index += packet.payload_len;
		bytes_remaining -= packet.payload_len;
		packet.pkt_sequence ++;
	}


	session.current_frame_id = (session.current_frame_id + 1) % 255;

	xSemaphoreGive(session_data_mutx);

	return ret_val;
}

int protocol_recv_ctrl(void** buf, struct sockaddr_in * source_addr)
{
	if (source_addr == NULL)
	{
		return -1;
	}

	if (xSemaphoreTake(socket_mutx, portMAX_DELAY) != pdTRUE)
	{
		ESP_LOGE(TAG, "Can't get socket mutx, shouldn't be here");
		return -1;
	}

//	struct sockaddr_in source_addr; // Large enough for both IPv4 or IPv6
	socklen_t socklen = sizeof(*source_addr);
	int recv_len = recvfrom(session.server.sock, session.server.rx_buffer, sizeof(session.server.rx_buffer) - 1, 0, (struct sockaddr *) source_addr, &socklen);

	if (recv_len < 0) // Error occurred during receiving
	{
		ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
	}
	else //if (source_addr.sin_addr.s_addr == session.server.local.sin_addr.s_addr)
	{
//		session.server.rx_buffer[recv_len] = 0; // Null-terminate whatever we received and treat like a string
		ESP_LOGI(TAG, "Received %d bytes from %s:", recv_len, session.server.addr_str);
//		ESP_LOGI(TAG, "%s", session.server.rx_buffer);
		*buf = session.server.rx_buffer;
//		session.server.remote = source_addr;
	}

	BaseType_t ret = xSemaphoreGive(socket_mutx);
	if (ret!= pdTRUE)
	{
		recv_len = -1;
	}

	return recv_len;
}

static void session_timeout_cb(void* arg)
{
	fsm_send_evt(&network_fsm, EVENT_SESSION_TIMEOUT, portMAX_DELAY);
}

static void session_keepalive(void)
{
	esp_timer_stop(session_timeout);
	esp_timer_start_once(session_timeout, NETWORK_SESSION_TIMEOUT_US);
}

static void session_keep_alive_stop(void)
{
	esp_timer_stop(session_timeout);
}

static void process_network_rcv(uint8_t * packet, int len, struct sockaddr_in * source)
{
	if (source == NULL || packet == NULL || len <= sizeof(protocol_packet_hdr_t))
	{
		return;
	}

	protocol_packet_hdr_t * pkt_header = (protocol_packet_hdr_t *) packet;
	if(source->sin_addr.s_addr == inet_addr(CONFIG_HOST_IP_ADDR) && pkt_header->frame_type == PROTOCOL_CTRL_PKT && pkt_header->payload_len > 0) //recognized address and packet type
	{
		ESP_LOGI(TAG, "Received valid msg");
		protocol_ctrl_payload_t cmd = packet[sizeof(protocol_packet_hdr_t)];

		switch (network_fsm.curr_state)
		{
		case STATE_SESSION_IDLE:
			if (cmd == PROTOCOL_STREAM_RQST)
			{
				memcpy(&session.server.remote, source, sizeof(session.server.remote));
				fsm_send_evt(&network_fsm, EVENT_STREAM_START_RQST, 0);
				//session rqst - send evt to fsm
			}
			break;
		case STATE_SESSION_STREAMING:
			if (cmd == PROTOCOL_STREAM_STOP)
			{
				fsm_send_evt(&network_fsm, EVENT_STREAM_STOP_RQST, 0);
			}
			else if (cmd == PROTOCOL_STREAM_KEEPALIVE)
			{
				fsm_send_evt(&network_fsm, EVENT_STREAM_KEEPALIVE, 0);
			}
			break;
		default:
			break;
		}
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
