#include "camera_module.h"

#include "esp_camera.h"
#include "system_mgmt.h"
#include "network_module.h"

#include "esp_log.h"

#include "jpeg.h"

#define CAMERA_MODULE_TASK_SIZE		2048

static const char* TAG = "camera_module";

jpeg_t frame_jpeg;
uint8_t jpeg_buf[CONFIG_JPEG_BUF_SIZE_MAX];

//static uint8_t camera_task_stack[CAMERA_MODULE_TASK_SIZE];
//static StaticTask_t camera_task_buffer;
static void camera_module_task(void *pv_parameter);
TaskHandle_t camera_task;

esp_err_t camera_module_init()
{
	esp_err_t ret_val = ESP_OK;

    ret_val = gpio_install_isr_service(0);
    if (ret_val != ESP_OK && ret_val != ESP_ERR_INVALID_STATE)
    {
    	printf("gpio isr service installation not successful\n");
    	return ret_val;
    }
	printf("bp 4 s state %p\n", get_s_state());

    ret_val = ESP_OK;

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
        .fb_count = 2 //if more than one, i2s runs in continuous mode. Use only with JPEG
    };

    ESP_ERROR_CHECK(esp_camera_init(&camera_config));

    xTaskCreatePinnedToCore(camera_module_task, "camera_module_task", 2048, NULL, CAMERA_TASK_PRIO, &camera_task, 1);
//	camera_task = xTaskCreateStaticPinnedToCore(camera_module_task, "camera_module_task", CAMERA_MODULE_TASK_SIZE, NULL, CAMERA_TASK_PRIO, (StackType_t*)camera_task_stack, (StaticTask_t*) &camera_task_buffer, 1);

	return ret_val;
}

static void camera_module_task(void *pv_parameter)
{
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); //for startup

    static uint8_t test_val = 0;

    frame_jpeg.buf = jpeg_buf;
    frame_jpeg.buf_max_size = CONFIG_JPEG_BUF_SIZE_MAX;
    frame_jpeg.buf_written_size = 0;

	while(1)
	{
        TickType_t fb_get_time = xTaskGetTickCount();
	    camera_fb_t * fb = esp_camera_fb_get();
	    fb_get_time = xTaskGetTickCount() - fb_get_time;
	    if (fb == NULL)	//if fb is null -> send err event to fsm handle
	    {
	    	hsm_send_evt_urgent(&hsm_system_mgmt, EVENT_FAULT, portMAX_DELAY);
	    	ESP_LOGE(TAG, "NULL frame");
	    	continue;
	    }

	    esp_err_t jpeg_ret_code;
        TickType_t jpeg_enc_time = xTaskGetTickCount();
	    jpeg_encode(fb->buf, fb->len, fb->width, fb->height, &frame_jpeg);
        jpeg_enc_time = xTaskGetTickCount() - jpeg_enc_time;
        ESP_LOGI(TAG, "fb get took %d0 ms, jpeg encoding took %d0 ms, the file size is %d", fb_get_time, jpeg_enc_time, frame_jpeg.buf_written_size);

//	    if (test_val == 0)
//	    {
//	    	for (uint32_t i = 0; i < fb->len; i++)
//	    	{
//	    		printf("%d,", fb->buf[i]);
//	    		if ((i % 32) == 0)
//	    			printf("\n");
//	    	}
//
//	    	printf("\n\n");
//
////	    	if (done)
////	    	{
////	    		for (uint32_t i = 0; i < 38400; i ++)
////	    		{
////		    		printf("%d,", dst_sample2[i]);
////		    		if ((i % 32) == 0)
////		    			printf("\n");
////	    		}
////	    	}
//
//	    	printf("\n\n");
//
//	    	test_val = 1;
//	    }
        network_module_generic_data_t data;
        data.buf = (void *) frame_jpeg.buf;
        data.size = frame_jpeg.buf_written_size;

	    xQueueSend(network_data_send_queue, (void *) &data, 0);

//	    vTaskDelay(200/portTICK_PERIOD_MS);
	    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

	    esp_camera_fb_return(fb);

	}
}
