#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"

#define CAMERA_MODULE_BASE		40

#define CAMERA_TASK_PRIO		5

extern TaskHandle_t camera_task;

esp_err_t camera_module_init();



#endif
