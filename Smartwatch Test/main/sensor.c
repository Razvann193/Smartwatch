#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sensor.h"
#include "esp_mac.h" // Added for MAC address functionality

#define MAX_VALUES 50

static int bpm_data[MAX_VALUES];
static int index = 0;
SemaphoreHandle_t data_mutex;

void sensor_task(void *pvParameters)
{
    data_mutex = xSemaphoreCreateMutex();
    while (1)
    {
        int fake_bpm = 60 + (rand() % 40); // Generate fake BPM data
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        if (index < MAX_VALUES)
        {
            bpm_data[index++] = fake_bpm;
        }
        else
        {
            // Remove the oldest value and add the new one
            for (int i = 1; i < MAX_VALUES; i++)
            {
                bpm_data[i - 1] = bpm_data[i];
            }
            bpm_data[MAX_VALUES - 1] = fake_bpm;
        }
        xSemaphoreGive(data_mutex);
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Simulate 1-second sensor read
    }
}

void get_bpm_data(int *buffer, int *size)
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    for (int i = 0; i < index; i++)
    {
        buffer[i] = bpm_data[i];
    }
    *size = index;
    xSemaphoreGive(data_mutex);
}

void clear_bpm_data()
{
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    index = 0;
    xSemaphoreGive(data_mutex);
}

void Demo_Task(void *arg)
{
    while (1)
    {
        printf("Demo_Task printing..\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}