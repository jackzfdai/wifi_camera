#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdlib.h>
#include <stdint.h>
#include "esp_err.h"

#define PROTOCOL_FRAME_SIZE 			1024
#define PROTOCOL_HEADER_SIZE			16
#define PROTOCOL_MAX_PAYLOAD_SIZE		((PROTOCOL_FRAME_SIZE)-(PROTOCOL_HEADER_SIZE))

typedef enum
{
	PROTOCOL_OK,
	PROTOCOL_ERR_CHN_BUSY,
	PROTOCOL_ERR_NO_MEM,
	PROTOCOL_ERR_NOT_CONNECTED,
	PROTOCOL_ERR_INVALID_ARG,
	PROTOCOL_ERR_GENERIC
} protocol_err_t;

typedef enum
{
	PROTOCOL_CTRL_PKT = 0xF,
	PROTOCOL_DATA_PKT,
	PROTOCOL_ERR_PKT
} protocol_pkt_type_t;

typedef enum
{
	PROTOCOL_CONNECT_RQST = 0xF,
	PROTOCOL_DISCONNECT_RQST,
	PROTOCOL_CONNECTED,
	PROTOCOL_STREAM_RQST,
	PROTOCOL_STREAM_STOP,
	PROTOCOL_STREAMING
} protocol_ctrl_payload_t;

typedef enum
{
	PROTOCOL_EVT_SESSION_STARTED = 0xF,
	PROTOCOL_EVT_SESSION_ENDED,
	PROTOCOL_EVT_STREAM_RQST,
	PROTOCOL_EVT_STREAM_STOP
} protocol_evt_t;

typedef struct
{
	void (*evt_handler)(protocol_evt_t evt);
} protocol_init_t;

typedef union
{
	struct
	{
		uint8_t frame_id;
		uint8_t frame_type;
		uint8_t total_packets;
		uint8_t pkt_sequence;
		int64_t local_timestamp_ms; //only updated for new frame
		uint32_t payload_len;
	};
	uint8_t val [PROTOCOL_HEADER_SIZE];
} protocol_packet_hdr_t;

esp_err_t protocol_session_init(protocol_init_t * init);

esp_err_t protocol_send_data(void * buf, uint32_t len);

esp_err_t protocol_send_ctrl(protocol_ctrl_payload_t ctrl);

int protocol_recv_ctrl (void ** buf);

#endif
