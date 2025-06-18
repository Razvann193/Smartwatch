#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mqtt_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "mqtt.h"
#include <time.h>
#define TAG "MQTT"

// Wi-Fi credentials
#define WIFI_SSID "teamHexagon"
#define WIFI_PASS "hexagon6"

// MQTT broker URI
#define MQTT_BROKER_URI "mqtt://broker.hivemq.com:1883"

// MQTT tooic
#define MQTT_TOPIC "hexagon"

// Global BPM value
int global_bpm = 60;

// Handles and queue/mutex from main.c
extern QueueHandle_t bpmQueue;
extern SemaphoreHandle_t bpmMutex;
extern TaskHandle_t dataSendTaskHandle;

static esp_mqtt_client_handle_t client;

// Function to connect to Wi-Fi
void wifi_init() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    ESP_ERROR_CHECK(esp_wifi_connect());
}

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            esp_mqtt_client_subscribe(client, MQTT_TOPIC, 0); // Subscribe to the topic
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Received data on topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "Message: %.*s", event->data_len, event->data);

            // Directly parse the received integer data
            char buffer[16] = {0}; // Temporary buffer to store the received data
            snprintf(buffer, sizeof(buffer), "%.*s", event->data_len, event->data); // Copy the received data
            global_bpm = strtol(buffer, NULL, 10); // Convert to integer using strtol
            ESP_LOGI(TAG, "Updated global BPM to: %d", global_bpm);
            break;

        default:
            break;
    }
}

// Initialize MQTT client
void mqtt_init() {
    const esp_mqtt_client_config_t mqtt_cfg = {}; // Minimal configuration
    client = esp_mqtt_client_init(&mqtt_cfg);

    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }

    // Set the MQTT broker URI dynamically
    esp_err_t err = esp_mqtt_client_set_uri(client, MQTT_BROKER_URI);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MQTT URI: %s", esp_err_to_name(err));
        return;
    }

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

// This task waits for BPM values from the queue and sends them to MQTT
void data_send_task(void *pvParameters) {
    int bpm_val = 0;
    while (1) {
        // Wait for a BPM value from the queue (waits up to 5 seconds)
        if (xQueueReceive(bpmQueue, &bpm_val, pdMS_TO_TICKS(5000)) == pdPASS) {
            // Try to lock the mutex for 100ms so we can safely update global_bpm
            if (xSemaphoreTake(bpmMutex, pdMS_TO_TICKS(100))) {
                global_bpm = bpm_val;
                xSemaphoreGive(bpmMutex); // Always unlock!
            }
            char message[50];
            snprintf(message, sizeof(message), "%d", bpm_val);
            esp_mqtt_client_publish(client, MQTT_TOPIC, message, 0, 0, 0);
            ESP_LOGI(TAG, "Published: %s (from task: %s)", message, pcTaskGetName(NULL));
        } else {
            // If nothing new, just send the current value
            if (xSemaphoreTake(bpmMutex, pdMS_TO_TICKS(100))) {
                bpm_val = global_bpm;
                xSemaphoreGive(bpmMutex);
            }
            char message[50];
            snprintf(message, sizeof(message), "%d", bpm_val);
            esp_mqtt_client_publish(client, MQTT_TOPIC, message, 0, 0, 0);
            ESP_LOGI(TAG, "Published (timeout): %s", message);
        }

        // Wait for a notification from the random BPM task, or timeout after 5 seconds
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
    }
}

// This task makes up a new BPM every 5 seconds and puts it in the queue
void random_bpm_task(void *pvParameters) {
    srand((unsigned int)time(NULL));
    int new_bpm = 0;
    while (1) {
        new_bpm = 60 + (rand() % 101);

        // Try to put the new BPM in the queue (if it's full, just skip it)
        if (xQueueSend(bpmQueue, &new_bpm, 0) != pdPASS) {
            printf("BPM queue full, skipping value!\n");
        }

        // Tell the data send task that there's something new (notification)
        xTaskNotifyGive(dataSendTaskHandle);

        // Also update global_bpm for BLE (need to lock it first)
        if (xSemaphoreTake(bpmMutex, pdMS_TO_TICKS(100))) {
            global_bpm = new_bpm;
            xSemaphoreGive(bpmMutex);
        }

        ESP_LOGI(TAG, "Randomly updated global BPM to: %d (from task: %s)", new_bpm, pcTaskGetName(NULL));
        vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds before next BPM
    }
}

