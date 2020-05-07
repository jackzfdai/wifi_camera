#include "camera_module.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_camera.h"
#include "system_mgmt.h"
#include "network_module.h"

#include "esp_log.h"

#include "jpeg.h"

#define CAMERA_MODULE_TASK_SIZE		2048

//jpeg frame queue used to enforce FIFO in the available frame stream, which is continuously provided by the jpeg encode task and camera driver
//mutex is also used on each available frame buffer to prevent misuse by external parties - ie attempting to return the same frame twice w/o
//first getting it
typedef struct
{
	jpeg_t frame;
	BaseType_t checked_out;
} jpeg_frame_ctrl_t;

static jpeg_frame_ctrl_t jpeg_frames_ctrl[CONFIG_NUM_JPEG_BUFFERS];

uint8_t jpeg_buf[CONFIG_NUM_JPEG_BUFFERS][CONFIG_JPEG_BUF_SIZE_MAX];

static QueueHandle_t jpeg_out_queue; //queues store index of frame inside the jpeg_frames_ctrl data structure
static StaticQueue_t jpeg_out_queue_data;
static uint32_t jpeg_out_queue_buffer[CONFIG_NUM_JPEG_BUFFERS];
static QueueHandle_t jpeg_in_queue;
static StaticQueue_t jpeg_in_queue_data;
static uint32_t jpeg_in_queue_buffer[CONFIG_NUM_JPEG_BUFFERS];

static const char* TAG = "camera_module";

static uint32_t find_frame_from_buf_adr (void * buf_adr);
static void jpeg_encode_task (void *parameters);

//static uint8_t camera_task_stack[CAMERA_MODULE_TASK_SIZE];
//static StaticTask_t camera_task_buffer;

//static void camera_module_task(void *pv_parameter);
TaskHandle_t camera_task;

esp_err_t camera_module_init()
{
	esp_err_t ret_val = ESP_OK;

	if (CONFIG_NUM_JPEG_BUFFERS == 0 || CONFIG_JPEG_BUF_SIZE_MAX == 0)
	{
		ret_val = ESP_ERR_INVALID_SIZE;
		return ret_val;
	}

    ret_val = gpio_install_isr_service(0);
    if (ret_val != ESP_OK && ret_val != ESP_ERR_INVALID_STATE)
    {
    	ESP_LOGE(TAG, "gpio isr service installation not successful");
    	return ret_val;
    }

    static camera_config_t camera_config = {
        .pin_pwdn  = -1,                        // power down is not used
        .pin_reset = CONFIG_RESET,              // software reset will be performed
        .pin_xclk = CONFIG_XCLK,
        .pin_sscb_sda = CONFIG_SDA,
        .pin_sscb_scl = CONFIG_SCL,

        .pin_d7 = CONFIG_D7,
        .pin_d6 = CONFIG_D6,
        .pin_d5 = CONFIG_D5,
        .pin_d4 = CONFIG_D4,
        .pin_d3 = CONFIG_D3,
        .pin_d2 = CONFIG_D2,
        .pin_d1 = CONFIG_D1,
        .pin_d0 = CONFIG_D0,
        .pin_vsync = CONFIG_VSYNC,
        .pin_href = CONFIG_HREF,
        .pin_pclk = CONFIG_PCLK,

        //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
        .xclk_freq_hz = CONFIG_XCLK_FREQ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_YUV422, /*PIXFORMAT_GRAYSCALE,*/ /*PIXFORMAT_RGB565*/
        .frame_size = FRAMESIZE_QQVGA, /*FRAMESIZE_QVGA,*/     //QQVGA-QXGA Do not use sizes above QVGA when not JPEG

        .jpeg_quality = 12, //0-63 lower number means higher quality
        .fb_count = 2 //if more than one, i2s runs in continuous mode.
    };

    ret_val = esp_camera_init(&camera_config);
    if (ret_val != ESP_OK)
    {
    	return ret_val;
    }

    jpeg_out_queue = xQueueCreateStatic(CONFIG_NUM_JPEG_BUFFERS, sizeof(uint32_t), (uint8_t*) jpeg_out_queue_buffer, &jpeg_out_queue_data);
    jpeg_in_queue = xQueueCreateStatic(CONFIG_NUM_JPEG_BUFFERS, sizeof(uint32_t), (uint8_t*) jpeg_in_queue_buffer, &jpeg_in_queue_data);

    if (jpeg_out_queue == NULL || jpeg_in_queue == NULL)
    {
    	ret_val = ESP_FAIL;
    	return ret_val;
    }

    for (uint32_t i = 0; i < CONFIG_NUM_JPEG_BUFFERS; i ++)
    {
    	jpeg_frames_ctrl[i].checked_out = pdFALSE;
		jpeg_frames_ctrl[i].frame.buf = jpeg_buf[i];
		jpeg_frames_ctrl[i].frame.buf_max_size = CONFIG_JPEG_BUF_SIZE_MAX;
		jpeg_frames_ctrl[i].frame.buf_written_size = 0;
        xQueueSend(jpeg_in_queue, (void *) &i, 0); //newly initialized buffers are ready to fill, send to "in" queue
    }

//    xTaskCreatePinnedToCore(camera_module_task, "camera_module_task", 2048, NULL, CAMERA_TASK_PRIO, &camera_task, 1);
//	camera_task = xTaskCreateStaticPinnedToCore(camera_module_task, "camera_module_task", CAMERA_MODULE_TASK_SIZE, NULL, CAMERA_TASK_PRIO, (StackType_t*)camera_task_stack, (StaticTask_t*) &camera_task_buffer, 1);

    xTaskCreatePinnedToCore(jpeg_encode_task, "jpeg_encode", 2048, NULL, CAMERA_TASK_PRIO, NULL, 1);

	return ret_val;
}

esp_err_t camera_get_jpeg(void** buf_adr, uint32_t* size, TickType_t xTicksToWait)
{
	esp_err_t ret_val = ESP_OK;

	uint32_t index = CONFIG_NUM_JPEG_BUFFERS;
	if (xQueueReceive(jpeg_out_queue, (void*) &index, xTicksToWait) != pdTRUE)
	{
		ret_val = ESP_ERR_TIMEOUT;
	}
	else if (index < CONFIG_NUM_JPEG_BUFFERS && jpeg_frames_ctrl[index].checked_out == pdFALSE)
	{
		*buf_adr = (void*) jpeg_frames_ctrl[index].frame.buf;
		*size = jpeg_frames_ctrl[index].frame.buf_written_size;
		jpeg_frames_ctrl[index].checked_out = pdTRUE;
		ret_val = ESP_OK;
	}
	else //shouldn't happen
	{
		ESP_LOGE(TAG, "Invalid state, unable to acquire lock on received frame buffer.");
		ret_val = ESP_ERR_INVALID_STATE;
	}
	return ret_val;
}

esp_err_t camera_return_jpeg(void *buf_adr)
{
	esp_err_t ret_val = ESP_OK;

	uint32_t index = find_frame_from_buf_adr(buf_adr);
	if (index >= CONFIG_NUM_JPEG_BUFFERS) //buffer address not found
	{
		ret_val = ESP_ERR_INVALID_ARG;
	}
	else if (jpeg_frames_ctrl[index].checked_out == pdTRUE)
	{
		jpeg_frames_ctrl[index].checked_out = pdFALSE;
	    xQueueSend(jpeg_in_queue, (void *) &index, 0); //guaranteed to succeed given queue size is the number of available buffers and protection from repeated returns w/o additional checkout by mutex
	    buf_adr = NULL;
	    ret_val = ESP_OK;
	}
	else
	{
		ESP_LOGE(TAG, "Invalid state, cannot return frame buffer without first acquiring it.");
		ret_val = ESP_ERR_INVALID_STATE;
	}
	return ret_val;
}

static uint32_t find_frame_from_buf_adr (void * buf_adr)
{
	uint32_t index = CONFIG_NUM_JPEG_BUFFERS;
	for (uint32_t i = 0; i < CONFIG_NUM_JPEG_BUFFERS; i ++)
	{
		if (jpeg_frames_ctrl[i].frame.buf == buf_adr)
		{
			index = i;
			break;
		}
	}
	return index;
}


static void jpeg_encode_task (void *parameters)
{
	while (1)
	{
	    camera_fb_t * fb = esp_camera_fb_get(); //this function is blocking
	    if (fb == NULL)	//if fb is null -> send err event to fsm handle
	    {
//	    	hsm_send_evt_urgent(&hsm_system_mgmt, EVENT_FAULT, portMAX_DELAY);
	    	ESP_LOGE(TAG, "NULL frame");
	    	continue;
	    }

	    uint32_t index = CONFIG_NUM_JPEG_BUFFERS;
	    if (xQueueReceive(jpeg_in_queue, (void*) &index, 0) != pdTRUE)
	    {
	    	xQueueReceive(jpeg_out_queue, (void*) &index, 0);
	    }

	    if (index < CONFIG_NUM_JPEG_BUFFERS)
	    {
		    jpeg_encode(fb->buf, fb->len, fb->width, fb->height, &jpeg_frames_ctrl[index].frame);
		    xQueueSend(jpeg_out_queue, (void *) &index, 0); //guaranteed to succeed given queue size is the number f available buffers
	    }

	    esp_camera_fb_return(fb);
	    portYIELD(); //vtaskdelay?
	}
}

//static uint32_t find_jpeg_buf_index()

//static void camera_module_task(void *pv_parameter)
//{
//    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); //for startup
//
//    static uint8_t test_val = 0;
//
//    frame_jpeg.buf = jpeg_buf;
//    frame_jpeg.buf_max_size = CONFIG_JPEG_BUF_SIZE_MAX;
//    frame_jpeg.buf_written_size = 0;
//
//	while(1)
//	{
//        TickType_t fb_get_time = xTaskGetTickCount();
//	    camera_fb_t * fb = esp_camera_fb_get();
//	    fb_get_time = xTaskGetTickCount() - fb_get_time;
//	    if (fb == NULL)	//if fb is null -> send err event to fsm handle
//	    {
//	    	hsm_send_evt_urgent(&hsm_system_mgmt, EVENT_FAULT, portMAX_DELAY);
//	    	ESP_LOGE(TAG, "NULL frame");
//	    	continue;
//	    }
//
//	    esp_err_t jpeg_ret_code;
//        TickType_t jpeg_enc_time = xTaskGetTickCount();
//	    jpeg_encode(fb->buf, fb->len, fb->width, fb->height, &frame_jpeg);
//        jpeg_enc_time = xTaskGetTickCount() - jpeg_enc_time;
//        ESP_LOGI(TAG, "fb get took %d0 ms, jpeg encoding took %d0 ms, the file size is %d", fb_get_time, jpeg_enc_time, frame_jpeg.buf_written_size);
//
////	    if (test_val == 0)
////	    {
////	    	for (uint32_t i = 0; i < fb->len; i++)
////	    	{
////	    		printf("%d,", fb->buf[i]);
////	    		if ((i % 32) == 0)
////	    			printf("\n");
////	    	}
////
////	    	printf("\n\n");
////
//////	    	if (done)
//////	    	{
//////	    		for (uint32_t i = 0; i < 38400; i ++)
//////	    		{
//////		    		printf("%d,", dst_sample2[i]);
//////		    		if ((i % 32) == 0)
//////		    			printf("\n");
//////	    		}
//////	    	}
////
////	    	printf("\n\n");
////
////	    	test_val = 1;
////	    }
//        network_module_generic_data_t data;
//        data.buf = (void *) frame_jpeg.buf;
//        data.size = frame_jpeg.buf_written_size;
//
//	    xQueueSend(network_data_send_queue, (void *) &data, 0);
//
////	    vTaskDelay(200/portTICK_PERIOD_MS);
//	    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
//
//	    esp_camera_fb_return(fb);
//
//	}
//}
