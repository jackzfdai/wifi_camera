#include "protocol.h"
#include "string.h"

#include "tcpip_adapter.h"

#include "esp_system.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "esp_log.h"
#include "esp_err.h"

static const char * TAG = "PROTOCOL";

/*----------private defines----------*/

/*----------private typedefs----------*/
typedef struct
{
    int addr_family;
    int ip_protocol;
    char rx_buffer[128];
    char addr_str[128];
	struct sockaddr_in dest_addr;
	int sock;
} udp_client_s;

//typedef struct
//{
//	int addr_family;
//	int ip_protocol;
//	char rx_buffer[128];
//	char addr_str[128];
//	struct sockaddr_in dest_addr;
//	int sock;
//} tcp_client_s;

typedef struct
{
	uint8_t init;
	uint8_t current_frame_id;
	udp_client_s udp_client;
	udp_client_s udp_ctrl_client;
//	tcp_client_s tcp_client;
	uint8_t * frame_buf;
	void (*evt_handler)(protocol_evt_t evt);
} m_protocol_ctrl;

m_protocol_ctrl session;

/*--------private variable declarations------------*/
static uint8_t protocol_frame_buf[PROTOCOL_FRAME_SIZE];

/*--------private functions declarations-----------*/
static esp_err_t session_udp_clients_init();
static esp_err_t session_udp_clients_destroy();
static esp_err_t session_udp_data_client_send(void* payload, size_t size);
static esp_err_t session_udp_ctrl_client_send(void);

static esp_err_t tcp_client_init (void);
static esp_err_t tcp_client_destroy (void);

//static void fill_header(protocol_packet_hdr_t * header, void * frame_buffer, uint32_t frame_buffer_size);

/*-----------public function definitions---------------*/

esp_err_t protocol_session_init(protocol_init_t * init)
{
	if (init == NULL)
	{
		return ESP_ERR_INVALID_ARG;
	}

	if (sizeof(protocol_packet_hdr_t) > PROTOCOL_FRAME_SIZE)
	{
		return ESP_ERR_INVALID_SIZE;
	}

	session.evt_handler = init->evt_handler;
	session.current_frame_id = 0;
	session.frame_buf = protocol_frame_buf;
	session.init = 1;

	esp_err_t ret_code = session_udp_clients_init();
	if (ret_code != ESP_OK)
	{
		session_udp_clients_destroy();
		return ret_code;
	}

	return ret_code;
}

esp_err_t protocol_send_data(void * buf, uint32_t len)
{
	if (buf == NULL || len == 0)
		return ESP_ERR_INVALID_ARG;

	if (!session.init)
		return ESP_ERR_INVALID_STATE;

	protocol_packet_hdr_t packet;

	packet.frame_id = session.current_frame_id;
	packet.frame_type = PROTOCOL_DATA_PKT;
	packet.pkt_sequence = 1;
	packet.total_packets = (len - 1)/PROTOCOL_MAX_PAYLOAD_SIZE + 1;
	packet.local_timestamp_ms = xTaskGetTickCount() * 10; //resolution is 10ms since freertos tick is set to 100hz

	uint32_t bytes_remaining = len;
	uint32_t payload_data_index = 0;

	esp_err_t ret_code = ESP_OK;

	uint8_t * buf_8b = (uint8_t *) buf;

	for (uint32_t pkt_num = 0; pkt_num < packet.total_packets; pkt_num ++)
	{


		if (bytes_remaining > PROTOCOL_MAX_PAYLOAD_SIZE)
			packet.payload_len = PROTOCOL_MAX_PAYLOAD_SIZE;
		else
			packet.payload_len = bytes_remaining;

//		fill_header(&packet, (void*) session.frame_buf, PROTOCOL_FRAME_SIZE);
		memcpy((void *) session.frame_buf, (void *) packet.val, PROTOCOL_HEADER_SIZE);
		memcpy((void *) &session.frame_buf[PROTOCOL_HEADER_SIZE], (void *) &buf_8b[payload_data_index], packet.payload_len);

		ret_code = session_udp_data_client_send(session.frame_buf, packet.payload_len + PROTOCOL_HEADER_SIZE);
		if (ret_code != ESP_OK)
			break;

		payload_data_index += packet.payload_len;
		bytes_remaining -= packet.payload_len;
		packet.pkt_sequence ++;
	}


	session.current_frame_id = (session.current_frame_id + 1) % 255;

	return ret_code;
}

int protocol_recv_ctrl(void** buf)
{
	struct sockaddr_in source_addr; // Large enough for both IPv4 or IPv6
	socklen_t socklen = sizeof(source_addr);
	int recv_len = recvfrom(session.udp_ctrl_client.sock, session.udp_ctrl_client.rx_buffer, sizeof(session.udp_ctrl_client.rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

	if (recv_len < 0) // Error occurred during receiving

	{
		ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
	}
	else if (source_addr.sin_addr.s_addr == session.udp_ctrl_client.dest_addr.sin_addr.s_addr)
	{
		session.udp_ctrl_client.rx_buffer[recv_len] = 0; // Null-terminate whatever we received and treat like a string
		ESP_LOGI(TAG, "Received %d bytes from %s:", recv_len, session.udp_ctrl_client.addr_str);
		ESP_LOGI(TAG, "%s", session.udp_ctrl_client.rx_buffer);
		*buf = session.udp_ctrl_client.rx_buffer;
	}

	return recv_len;
}

/*----------private functin definitions-----------------*/
static esp_err_t session_udp_clients_init()
{
	esp_err_t ret_val = ESP_OK;

    session.udp_client.dest_addr.sin_addr.s_addr = inet_addr(CONFIG_HOST_IP_ADDR);
    session.udp_client.dest_addr.sin_family = AF_INET;
    session.udp_client.dest_addr.sin_port = htons(CONFIG_PORT);
    session.udp_client.addr_family = AF_INET;
    session.udp_client.ip_protocol = IPPROTO_IP;
    inet_ntoa_r(session.udp_client.dest_addr.sin_addr, session.udp_client.addr_str, sizeof(session.udp_client.addr_str) - 1);

    session.udp_ctrl_client.dest_addr.sin_addr.s_addr = inet_addr(CONFIG_HOST_IP_ADDR);
    session.udp_ctrl_client.dest_addr.sin_family = AF_INET;
    session.udp_ctrl_client.dest_addr.sin_port = htons(CONFIG_CTRL_PORT);
    session.udp_ctrl_client.addr_family = AF_INET;
    session.udp_ctrl_client.ip_protocol = IPPROTO_IP;
    inet_ntoa_r(session.udp_ctrl_client.dest_addr.sin_addr, session.udp_ctrl_client.addr_str, sizeof(session.udp_ctrl_client.addr_str) - 1);

    session.udp_client.sock = socket(session.udp_client.addr_family, SOCK_DGRAM, session.udp_client.ip_protocol);
    session.udp_ctrl_client.sock = socket(session.udp_ctrl_client.addr_family, SOCK_DGRAM, session.udp_ctrl_client.ip_protocol);

    if (session.udp_client.sock < 0 || session.udp_ctrl_client.sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        ret_val = ESP_FAIL;
    }
    else
    {
        ESP_LOGI(TAG, "Socket created, sending to %s:%d", CONFIG_HOST_IP_ADDR, CONFIG_PORT);
    }

    return ret_val;
}

static esp_err_t session_udp_clients_destroy()
{
	esp_err_t ret_val = ESP_OK;

    if (session.udp_client.sock != -1)
    {
        shutdown(session.udp_client.sock, 0);
        close(session.udp_client.sock);
    }

    if (session.udp_ctrl_client.sock != -1)
    {
        shutdown(session.udp_ctrl_client.sock, 0);
        close(session.udp_ctrl_client.sock);
    }

	return ret_val;
}

static esp_err_t session_udp_data_client_send(void* payload, size_t size)
{
	esp_err_t ret_val = ESP_OK;

	int err = sendto(session.udp_client.sock, payload, size, 0, (struct sockaddr *)&session.udp_client.dest_addr, sizeof(session.udp_client.dest_addr));
	if (err < 0) {
		ret_val = ESP_FAIL;
		ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
	}
	else
	{
		ESP_LOGI(TAG, "Message sent");
	}

	return ret_val;
}

