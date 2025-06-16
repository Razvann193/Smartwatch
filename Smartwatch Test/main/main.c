#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "ble.h"
#include "mqtt.h"
#include "esp_mac.h" 

// Task handles for notifications or stack checks)
TaskHandle_t dataSendTaskHandle = NULL;
TaskHandle_t randomBpmTaskHandle = NULL;
TaskHandle_t healthMonitorTaskHandle = NULL;

// This is a queue for sending BPM values between tasks
QueueHandle_t bpmQueue = NULL;

// Mutex so two tasks don't mess with global_bpm at the same time
SemaphoreHandle_t bpmMutex = NULL;

// This task just checks how much stack is left for other tasks (helps spot bugs or stack overflows)
// void health_monitor_task(void *pvParameters) {
//     while (1) {
//         // Prints out how much stack is left for each task (in words)
//         printf("[Health] DataSendTask stack left: %lu\n", uxTaskGetStackHighWaterMark(dataSendTaskHandle));
//         printf("[Health] RandomBpmTask stack left: %lu\n", uxTaskGetStackHighWaterMark(randomBpmTaskHandle));
//         vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds (pdMS_TO_TICKS convert ms to ticks)
//     }
// }

void app_main(void)
{
    ble_init();
    wifi_init();
    mqtt_init();

    // Make a queue that can hold 5 ints (should be enough for bpm values)
    bpmQueue = xQueueCreate(5, sizeof(int));
    if (!bpmQueue) printf("Failed to create BPM queue!\n");

    // Make a mutex 
    bpmMutex = xSemaphoreCreateMutex();
    if (!bpmMutex) printf("Failed to create BPM mutex!\n");

    // Start the tasks. Stack size is in words not bytes! 512 = 2KB, 1024 = 4KB.
    // Priority: higher = more important. Handles are for controling or cheecking the tasks later.
    xTaskCreate(data_send_task, "Data Send Task", 1024, NULL, 5, &dataSendTaskHandle);
    xTaskCreate(random_bpm_task, "Random BPM Task", 1024, NULL, 10, &randomBpmTaskHandle);
    xTaskCreate(health_monitor_task, "Health Monitor Task", 768, NULL, 1, &healthMonitorTaskHandle);
}