#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ble.h"
#include "mqtt.h"
//#include "sensor.h"
#include "esp_mac.h" 

void app_main(void)
{
    // Initialize BLE
    ble_init();

    wifi_init();
    // Initialize MQTT
    mqtt_init();

    // Create FreeRTOS tasks
    // xTaskCreate(sensor_task, "Sensor Task", 2048, NULL, 5, NULL);
    xTaskCreate(data_send_task, "Data Send Task", 4096, NULL, 5, NULL);
    xTaskCreate(random_bpm_task, "Random BPM Task", 2048, NULL, 10, NULL);
}