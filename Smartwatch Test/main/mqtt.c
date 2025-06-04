#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mqtt_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt.h"
#include <time.h>
#define TAG "MQTT"

// Wi-Fi credentials
#define WIFI_SSID "teamHexagon"
#define WIFI_PASS "hexagon6"

// MQTT broker URI
#define MQTT_BROKER_URI "mqtt://broker.hivemq.com:1883"

// MQTT topic
#define MQTT_TOPIC "hexagon"

// Global BPM value
int global_bpm = 60;

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

// Task to publish fake BPM values
void data_send_task(void *pvParameters) {
    while (1) {
        char message[50];
        snprintf(message, sizeof(message), "%d", global_bpm); // Use the global BPM value
        esp_mqtt_client_publish(client, MQTT_TOPIC, message, 0, 0, 0); // Publish with default QoS and retain
        ESP_LOGI(TAG, "Published: %s", message);

        vTaskDelay(5000 / portTICK_PERIOD_MS); // Publish every 5 seconds
    }
}

// Task to randomly update BPM every 5 seconds
void random_bpm_task(void *pvParameters) {
    srand((unsigned int)time(NULL)); // Seed the random number generator once
    while (1) {
        global_bpm = 60 + (rand() % 101); // Generate a random value between 60 and 160
        ESP_LOGI(TAG, "Randomly updated global BPM to: %d", global_bpm);
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Wait for 5 seconds
    }
}
