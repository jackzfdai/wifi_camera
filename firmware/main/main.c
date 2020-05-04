#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "state_machine.h"
#include "system_mgmt.h"
//#include "camera_mgmt.h"
#include "esp_camera.h"

static void task_state_to_string (eTaskState state, char * strn_buf);
static void stats_task(void * pvParam);

void app_main()
{
	vTaskDelay(pdMS_TO_TICKS(100));
//    printf("Hello world!\n");

    /* Print chip information */
//    esp_chip_info_t chip_info;
//    esp_chip_info(&chip_info);
//    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
//            chip_info.cores,
//            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
//            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
//
//    printf("silicon revision %d, ", chip_info.revision);
//
//    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
//            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

	ESP_ERROR_CHECK(system_mgmt_init(NULL)); // , NULL));
	vTaskDelay(pdMS_TO_TICKS(100));
//	xTaskCreatePinnedToCore(stats_task,"stats task",4096,NULL,8, NULL, 1);
	vTaskDelay(pdMS_TO_TICKS(100));
//    printf("insufficient ram");
//    for (int i = 10; i >= 0; i--) {
//        printf("Restarting in %d seconds...\n", i);
//        vTaskDelay(1000 / portTICK_PERIOD_MS);
//    }
//    printf("Restarting now.\n");
//    fflush(stdout);
//    esp_restart();

	while(1)
	{
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}

//static void mem_monitor_task (void * pvParam)
//{
//	while(1)
//	{
//		vTaskDelay(1000/portTICK_PERIOD_MS);
//	}
//}

static void stats_task(void * pvParam)
{
	while(1)
	{
		TaskStatus_t task_stat_array[20];
		uint32_t total_runtime = 0;
		uint32_t num_tasks = 0;
	    //heap_caps_print_heap_info(MALLOC_CAP_DMA);
	    num_tasks = uxTaskGetSystemState(task_stat_array, 20, &total_runtime);

	    printf("\ntotal_runtime %d\n", total_runtime);
	    printf("      Task Name      | Task Status | Task Stack Base | Task Stack HWM | Core ID | pc Runtime \n");
	    for (uint32_t i = 0; i < num_tasks; i ++)
	    {
	    	char task_stat [24] = "\0";
	    	task_state_to_string(task_stat_array[i].eCurrentState, task_stat);
	    	printf(" %19s | %11s | %15p | %15d | %7d | %10d \n", task_stat_array[i].pcTaskName, task_stat, task_stat_array[i].pxStackBase, task_stat_array[i].usStackHighWaterMark, task_stat_array[i].xCoreID, task_stat_array[i].ulRunTimeCounter*100/total_runtime );
	    }

	    printf("\n");
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}

static void task_state_to_string (eTaskState state, char * strn_buf)
{
	switch(state)
	{
	case eReady:
		sprintf(strn_buf, "Ready");
		break;
	case eRunning:
		sprintf(strn_buf, "Running");
		break;
	case eBlocked:
		sprintf(strn_buf, "Blocked");
		break;
	case eSuspended:
		sprintf(strn_buf, "Suspended");
		break;
	case eDeleted:
		sprintf(strn_buf, "Deleted");
		break;
	default:
		sprintf(strn_buf, "Unknown");
		break;
	}
}
