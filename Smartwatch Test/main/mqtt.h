#ifndef MQTT_H
#define MQTT_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern int global_bpm;

void mqtt_init();
void wifi_init();
void data_send_task(void *pvParameters);
void random_bpm_task(void *pvParameters);

#endif