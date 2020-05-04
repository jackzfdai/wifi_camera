#include "jpeg.h"

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char * TAG = "JPEG";

static void bitstream_2d_convert(uint32_t total_len, uint32_t height, uint8_t * bitstream, uint8_t ** bitstream_2d);
static void yuv422_get_Y_pix_block(uint32_t pix_origin_row, uint32_t pix_origin_col, uint8_t block_len_row, uint8_t block_len_col, uint8_t ** bitstream_2d_in, short * output_buf);
void YUV422_get_Cr_pix_block(uint32_t pix_origin_row, uint32_t pix_origin_col, uint8_t pix_block_len_row, uint8_t pix_block_len_col, uint8_t ** bitstream_2d_in, short * output_buf);
void YUV422_get_Cb_pix_block(uint32_t pix_origin_row, uint32_t pix_origin_col, uint8_t pix_block_len_row, uint8_t pix_block_len_col, uint8_t ** bitstream_2d_in, short * output_buf);

typedef struct
{
	jpeg_t * jpeg_out;
	uint32_t frame_pix_width;
	uint32_t frame_pix_height;
	uint32_t frame_byte_per_pix;
	volatile esp_err_t status;
} m_jpeg_ctrl;

static m_jpeg_ctrl jpeg;

//uint32_t jpeg_encode(uint8_t * input_buf, uint32_t input_buf_size, uint8_t * jpeg_buf, uint32_t jpeg_buf_size, uint32_t frame_width, uint32_t frame_height)
esp_err_t jpeg_encode(uint8_t * input_buf, uint32_t input_buf_size, uint32_t frame_width, uint32_t frame_height, jpeg_t * output)
{
	if (input_buf == NULL || output == NULL)
	{
		ESP_LOGE(TAG, "Null frame buffers for JPEG encode.");
		return ESP_ERR_INVALID_ARG;
	}

	if (output->buf_max_size == 0 || input_buf_size == 0 || frame_width == 0 || frame_height == 0)
	{
		ESP_LOGE(TAG, "Implausible image buffer size of frame dimensions.");
		return ESP_ERR_INVALID_ARG;
	}

	jpeg.frame_pix_height = frame_height;
	jpeg.frame_pix_width = frame_width;
	jpeg.jpeg_out = output;
	jpeg.jpeg_out->buf_written_size = 0;
	jpeg.frame_byte_per_pix = input_buf_size/frame_height/frame_width;
	jpeg.status = ESP_OK;

	short Y_8x8 [2][2][8][8];

	uint8_t * input_buf_2d[frame_height];

	bitstream_2d_convert(input_buf_size, jpeg.frame_pix_height, input_buf, input_buf_2d);

	huffman_start(jpeg.frame_pix_height & -JPEG_PIX_BLOCK_SIZE, jpeg.frame_pix_width & -JPEG_PIX_BLOCK_SIZE);

	for (uint32_t pix_position_row = 0; pix_position_row < jpeg.frame_pix_height - (JPEG_PIX_BLOCK_SIZE - 1); pix_position_row += JPEG_PIX_BLOCK_SIZE)
	for (uint32_t pix_position_col = 0; pix_position_col < jpeg.frame_pix_width - (JPEG_PIX_BLOCK_SIZE - 1); pix_position_col += JPEG_PIX_BLOCK_SIZE)
	{
		for (uint32_t block_row = 0; block_row < 2; block_row ++)
			for (uint32_t block_col = 0; block_col < 2; block_col ++)
			{
				yuv422_get_Y_pix_block(pix_position_row + block_row*8, pix_position_col + block_col*8, 8, 8, input_buf_2d, (short *) Y_8x8[block_row][block_col]);
			}

			short Cr_8x8 [8][8];
			YUV422_get_Cr_pix_block(pix_position_row, pix_position_col, 8, 8, input_buf_2d, (short*) Cr_8x8);

			short Cb_8x8 [8][8];
			YUV422_get_Cb_pix_block(pix_position_row, pix_position_col, 8, 8, input_buf_2d, (short*) Cb_8x8);

			// 1 Y-compression
			dct(Y_8x8[0][0], Y_8x8[0][0]);
			huffman_encode(HUFFMAN_CTX_Y, (short*)Y_8x8[0][0]);
			// 2 Y-compression
			dct(Y_8x8[0][1], Y_8x8[0][1]);
			huffman_encode(HUFFMAN_CTX_Y, (short*)Y_8x8[0][1]);
			// 3 Y-compression
			dct(Y_8x8[1][0], Y_8x8[1][0]);
			huffman_encode(HUFFMAN_CTX_Y, (short*)Y_8x8[1][0]);
			// 4 Y-compression
			dct(Y_8x8[1][1], Y_8x8[1][1]);
			huffman_encode(HUFFMAN_CTX_Y, (short*)Y_8x8[1][1]);
			// Cb-compression
			dct(Cb_8x8, Cb_8x8);
			huffman_encode(HUFFMAN_CTX_Cb, (short*)Cb_8x8);
			// Cr-compression
			dct(Cr_8x8, Cr_8x8);
			huffman_encode(HUFFMAN_CTX_Cr, (short*)Cr_8x8);

			if (jpeg.status != ESP_OK)
			{
				ESP_LOGE (TAG, "JPEG frame buffer too small, unable to fit entire JPEG frame.");
				jpeg.jpeg_out->buf_written_size = 0;
				return jpeg.status;
			}
	}

	huffman_stop();

	if (jpeg.status != ESP_OK)
	{
		ESP_LOGE (TAG, "JPEG frame buffer too small, unable to fit entire JPEG frame.");
		jpeg.jpeg_out->buf_written_size = 0;
	}

	return jpeg.status;
}

void write_jpeg(const unsigned char buff[], const unsigned size)
{
	if (jpeg.jpeg_out->buf_written_size + size > jpeg.jpeg_out->buf_max_size)
	{
		jpeg.status = ESP_ERR_NO_MEM;
		return;
	}

	uint8_t *write_buf = jpeg.jpeg_out->buf + jpeg.jpeg_out->buf_written_size; //&jpeg.jpeg_buf[jpeg.jpeg_bytes_written];
	for (uint32_t i = 0; i < size; i ++)
	{
		write_buf[i] = buff[i];
	}

	jpeg.jpeg_out->buf_written_size += size;
}

/* private functions */
static void bitstream_2d_convert(uint32_t total_len, uint32_t height, uint8_t * bitstream, uint8_t ** bitstream_2d)
{
	//converts to 2d_bitstream[row][col]
	uint32_t width = total_len/height;

	for (uint32_t row = 0; row < height; row ++)
		bitstream_2d[row] = &bitstream[row*width];
}

static void yuv422_get_Y_pix_block(uint32_t pix_origin_row, uint32_t pix_origin_col, uint8_t block_len_row, uint8_t block_len_col, uint8_t ** bitstream_2d_in, short * output_buf)
{

	if(output_buf == NULL || bitstream_2d_in == NULL)
	{
		ESP_LOGE(TAG, "Null input");
		return;
	}

	if (pix_origin_col + block_len_col > jpeg.frame_pix_width || pix_origin_row + block_len_row > jpeg.frame_pix_height)
	{
		ESP_LOGE(TAG, "YUV422 get y pix block exceeded frame bounds, col: %d row: %d\n", pix_origin_col, pix_origin_row);
		return;
	}

	for (uint32_t row = 0; row < block_len_row; row++)
	{
		uint8_t * input_row_array = bitstream_2d_in[pix_origin_row + row] + jpeg.frame_byte_per_pix * pix_origin_col;
		short * output_row_array = output_buf + row * block_len_col;

		for (uint32_t col = 0; col < block_len_col; col ++)
		{
			output_row_array[col] = input_row_array[2*col] - 128; //-128 to center the block about 0 for DCT
		}
	}
}

//Note Cr is U Y0_U0_Y1_V0
//expect to call for block len of 8x8
void YUV422_get_Cr_pix_block(uint32_t pix_origin_row, uint32_t pix_origin_col, uint8_t pix_block_len_row, uint8_t pix_block_len_col, uint8_t ** bitstream_2d_in, short * output_buf)
{
	if (output_buf == NULL || bitstream_2d_in == NULL)
	{
		ESP_LOGE(TAG, "YUV422 get Cr pix block null buffer");
		jpeg.status = ESP_ERR_INVALID_ARG;
		return;
	}
	if ((pix_origin_row + pix_block_len_row) > jpeg.frame_pix_height || (pix_origin_col + (pix_block_len_col << 1)) > jpeg.frame_pix_width)
	{
		ESP_LOGE(TAG, "YUV422 get Cr pix block exceeded frame bounds");
		jpeg.status = ESP_ERR_INVALID_ARG;
		return;
	}

	for (uint32_t row = 0; row < pix_block_len_row; row ++)
	{
		uint8_t * input_row_array_1 = bitstream_2d_in[pix_origin_row + (row << 1)] + jpeg.frame_byte_per_pix * pix_origin_col; //select row + x axis offset
		uint8_t * input_row_array_2 = bitstream_2d_in[pix_origin_row + (row << 1) + 1] + jpeg.frame_byte_per_pix * pix_origin_col; //select row + x axis offset
		short * output_row_array = output_buf + row * pix_block_len_col;
		// uint32_t output_offset = row * pix_block_len_col;
		for (uint32_t col = 0; col < pix_block_len_col; col ++)
		{
			short Cr_avg = (input_row_array_1[(col << 2)+3] + input_row_array_2[(col << 2)+3]) >> 1; //4*col + 3
			output_row_array[col] = Cr_avg - 128; //-128 to center the block about 0 for DCT
		}
	}
}

//Note Cb is V Y0_U0_Y1_V0
//expect to call for block len of 8x8
void YUV422_get_Cb_pix_block(uint32_t pix_origin_row, uint32_t pix_origin_col, uint8_t pix_block_len_row, uint8_t pix_block_len_col, uint8_t ** bitstream_2d_in, short * output_buf)
{
	if (output_buf == NULL || bitstream_2d_in == NULL)
	{
		ESP_LOGE(TAG, "YUV422 get Cb pix block null buffer");
		jpeg.status = ESP_ERR_INVALID_ARG;
		return;
	}
	if ((pix_origin_row + pix_block_len_row) > jpeg.frame_pix_height || (pix_origin_col + (pix_block_len_col << 1)) > jpeg.frame_pix_width)
	{
		ESP_LOGE(TAG, "YUV422 get Cb pix block exceeded frame bounds");
		jpeg.status = ESP_ERR_INVALID_ARG;
		return;
	}

	for (uint32_t row = 0; row < pix_block_len_row; row ++)
	{
		uint8_t * input_row_array_1 = bitstream_2d_in[pix_origin_row + (row << 1)] + jpeg.frame_byte_per_pix * pix_origin_col; //select row + x axis offset
		uint8_t * input_row_array_2 = bitstream_2d_in[pix_origin_row + (row << 1) + 1] + jpeg.frame_byte_per_pix * pix_origin_col; //select row + x axis offset
		short * output_row_array = output_buf + row * pix_block_len_col;
		// uint32_t output_offset = row * pix_block_len_col;
		for (uint32_t col = 0; col < pix_block_len_col; col ++)
		{
			short Cb_avg = (input_row_array_1[(col << 2)+1] + input_row_array_2[(col << 2)+1]) >> 1; //4*col + 1
			output_row_array[col] = Cb_avg - 128; //-128 to center the block about 0 for DCT
		}
	}

}
