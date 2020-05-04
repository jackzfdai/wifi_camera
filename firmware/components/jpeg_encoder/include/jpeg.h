#ifndef JPEG_H
#define JPEG_H

#include <stdlib.h>
#include <stdint.h>

#include "dct.h"
#include "jpegenc.h"

#include "esp_err.h"

typedef struct
{
	uint8_t * buf;
	uint32_t buf_written_size;
	uint32_t buf_max_size;
} jpeg_t;

//uint32_t jpeg_encode(uint8_t * input_buf, uint32_t input_buf_size, uint8_t * jpeg_buf, uint32_t jpeg_buf_size, uint32_t frame_width, uint32_t frame_height);

esp_err_t jpeg_encode(uint8_t * input_buf, uint32_t input_buf_size, uint32_t frame_width, uint32_t frame_height, jpeg_t * output);

#endif
