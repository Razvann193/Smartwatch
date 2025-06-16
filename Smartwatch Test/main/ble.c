#include <stdio.h>
#include <string.h>
#include "mqtt.h" 
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define TAG "BLE"
#define DEVICE_NAME "Hexagon Watch"
// Use the mutex to safely read global_bpm
extern SemaphoreHandle_t bpmMutex;
// BLE Profile
#define PROFILE_APP_ID 0

// UUIDs for the service and characteristics
static const uint16_t TEST_SERVICE_UUID = 0x1234;
static const uint16_t TEST_CHAR_UUID = 0x5678;
static const uint16_t EXTRA_CHAR_UUID = 0x9ABC;
static const uint16_t BROADCAST_CHAR_UUID = 0x2A37; // Heart Rate Measurement characteristic UUID

// BLE characteristic values
static int test_char_value = 0;
static int extra_char_value = 42;
static int broadcast_char_value = 60; // Initial BPM value

// BLE advertising parameters
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// BLE advertising data
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// GATT service ID
static esp_gatt_srvc_id_t service_id = {
    .is_primary = true,
    .id = {
        .inst_id = 0,
        .uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = { .uuid16 = TEST_SERVICE_UUID },
        },
    },
};

// BLE Profile
struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t service_handle;
    uint16_t char_handle;
    uint16_t extra_char_handle;
    uint16_t broadcast_char_handle; // Handle for the broadcsting characteristic
    uint16_t conn_id;
};

static struct gatts_profile_inst gl_profile = {
    .gatts_cb = NULL,
    .gatts_if = ESP_GATT_IF_NONE,
};

// GAP event handler
void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising data set complete. Starting advertising...");
            esp_err_t adv_start_ret = esp_ble_gap_start_advertising(&adv_params);
            if (adv_start_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start advertising: %s", esp_err_to_name(adv_start_ret));
            }
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising started successfully");
            } else {
                ESP_LOGE(TAG, "Failed to start advertising: %d", param->adv_start_cmpl.status);
            }
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising stopped. Restarting advertising...");
            esp_ble_gap_start_advertising(&adv_params); // Restart advertising
            break;

        default:
            break;
    }
}

// GATT event handler: handles BLE service/characteristic setup and client interactions
void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG, "GATT server registered");
            gl_profile.gatts_if = gatts_if;

            // Set the BLE device name that will show up when scanning
            esp_ble_gap_set_device_name(DEVICE_NAME);

            // Set up the advertising data (what is broadcasted to phones)
            esp_ble_gap_config_adv_data(&adv_data);

            // Create the main BLE service for this device
            // 10 is the number of handles (service + characteristics + descriptors)
            esp_err_t ret = esp_ble_gatts_create_service(gatts_if, &service_id, 10);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to create service: %s", esp_err_to_name(ret));
            }
            break;

        case ESP_GATTS_CREATE_EVT:
            if (param->create.status == ESP_GATT_OK) {
                ESP_LOGI(TAG, "Service created successfully, handle: %d", param->create.service_handle);
                gl_profile.service_handle = param->create.service_handle;

                // Add the first characteristic (for example, a test value)
                esp_bt_uuid_t char_uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = { .uuid16 = TEST_CHAR_UUID },
                };
                esp_err_t add_char_ret = esp_ble_gatts_add_char(
                    gl_profile.service_handle,
                    &char_uuid,
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, // Allow read and write from client
                    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE, // Client can read/write
                    NULL,
                    NULL
                );
                if (add_char_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to add characteristic: %s", esp_err_to_name(add_char_ret));
                }

                // Add a second characteristic (could be for extra data)
                esp_bt_uuid_t extra_char_uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = { .uuid16 = EXTRA_CHAR_UUID },
                };
                esp_err_t add_extra_char_ret = esp_ble_gatts_add_char(
                    gl_profile.service_handle,
                    &extra_char_uuid,
                    ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,
                    NULL,
                    NULL
                );
                if (add_extra_char_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to add extra characteristic: %s", esp_err_to_name(add_extra_char_ret));
                }

                // Add a third characteristic for broadcasting (for example, heart rate)
                esp_bt_uuid_t broadcast_char_uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = { .uuid16 = BROADCAST_CHAR_UUID },
                };
                esp_attr_value_t broadcast_char_val = {
                    .attr_max_len = sizeof(broadcast_char_value),
                    .attr_len = sizeof(broadcast_char_value),
                    .attr_value = (uint8_t *)&broadcast_char_value,
                };
                esp_err_t add_broadcast_char_ret = esp_ble_gatts_add_char(
                    gl_profile.service_handle,
                    &broadcast_char_uuid,
                    ESP_GATT_PERM_READ, // Only readable
                    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_BROADCAST, // Read and broadcast
                    &broadcast_char_val,
                    NULL
                );
                if (add_broadcast_char_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to add broadcasting characteristic: %s", esp_err_to_name(add_broadcast_char_ret));
                }
            } else {
                ESP_LOGE(TAG, "Failed to create service: %s", esp_err_to_name(param->create.status));
            }
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            if (param->add_char.status == ESP_GATT_OK) {
                ESP_LOGI(TAG, "Characteristic added successfully, handle: %d", param->add_char.attr_handle);

                // Figure out which characteristic was just added by checking its UUID
                if (param->add_char.char_uuid.uuid.uuid16 == TEST_CHAR_UUID) {
                    gl_profile.char_handle = param->add_char.attr_handle;
                } else if (param->add_char.char_uuid.uuid.uuid16 == EXTRA_CHAR_UUID) {
                    gl_profile.extra_char_handle = param->add_char.attr_handle;
                } else if (param->add_char.char_uuid.uuid.uuid16 == BROADCAST_CHAR_UUID) {
                    gl_profile.broadcast_char_handle = param->add_char.attr_handle;
                }

                // Start the service only after all characteristics are added
                if (gl_profile.char_handle && gl_profile.extra_char_handle && gl_profile.broadcast_char_handle) {
                    esp_ble_gatts_start_service(gl_profile.service_handle);
                }
            } else {
                ESP_LOGE(TAG, "Failed to add characteristic: %s", esp_err_to_name(param->add_char.status));
            }
            break;

        case ESP_GATTS_READ_EVT:
            ESP_LOGI(TAG, "Read request received, handle: %d", param->read.handle);
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
            rsp.attr_value.handle = param->read.handle;

            // Respond with the correct value depending on which characteristic is being read
            if (param->read.handle == gl_profile.char_handle) {
                rsp.attr_value.len = sizeof(test_char_value);
                memcpy(rsp.attr_value.value, &test_char_value, sizeof(test_char_value));
            } else if (param->read.handle == gl_profile.extra_char_handle) {
                rsp.attr_value.len = sizeof(extra_char_value);
                memcpy(rsp.attr_value.value, &extra_char_value, sizeof(extra_char_value));
            } else if (param->read.handle == gl_profile.broadcast_char_handle) {
                rsp.attr_value.len = sizeof(broadcast_char_value);
                memcpy(rsp.attr_value.value, &broadcast_char_value, sizeof(broadcast_char_value));
            }

            // Send the response back to the BLE client (like a phone app)
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            break;

        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(TAG, "Write request received, handle: %d, length: %d", param->write.handle, param->write.len);

            // Update the correct variable depending on which characteristic was written
            if (param->write.handle == gl_profile.char_handle) {
                if (param->write.len <= sizeof(test_char_value)) {
                    memcpy(&test_char_value, param->write.value, param->write.len);
                    ESP_LOGI(TAG, "Updated test_char_value: %d", test_char_value);
                } else {
                    ESP_LOGW(TAG, "Write request ignored: data length exceeds characteristic size");
                }
            } else if (param->write.handle == gl_profile.extra_char_handle) {
                if (param->write.len <= sizeof(extra_char_value)) {
                    memcpy(&extra_char_value, param->write.value, param->write.len);
                    ESP_LOGI(TAG, "Updated extra_char_value: %d", extra_char_value);
                } else {
                    ESP_LOGW(TAG, "Write request ignored: data length exceeds characteristic size");
                }
            } else {
                ESP_LOGW(TAG, "Write request ignored: invalid handle");
            }

            // Always send a response to the client after a write
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            break;

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "Device connected");
            gl_profile.conn_id = param->connect.conn_id;
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Device disconnected. Restarting advertising...");
            esp_ble_gap_start_advertising(&adv_params); // Restart advertising so new devices can connect
            break;

        default:
            break;
    }
}

// This task updates the BLE broadcast value every 10 seconds
void update_broadcast_char_value_task(void *pvParameters) {
    while (1) {
        // Try to lock the mutex for 100ms before reading global_bpm
        if (xSemaphoreTake(bpmMutex, pdMS_TO_TICKS(100))) {
            broadcast_char_value = global_bpm;
            xSemaphoreGive(bpmMutex);
        }
        ESP_LOGI(TAG, "Updated broadcast_char_value to: %d", broadcast_char_value);
        esp_ble_gatts_set_attr_value(gl_profile.broadcast_char_handle, sizeof(broadcast_char_value), (uint8_t *)&broadcast_char_value);
        vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds before next update
    }
}
// BLE initialization
void ble_init() {

    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Set up and enable the Bluetooth controller in BLE mode
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    // Initialize and enable the Bluedroid stack (ESP-IDF's Bluetooth stack)
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Register the GAP (advertising, connection) and GATT (services, characteristics) event handlers
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(PROFILE_APP_ID));

    // Start the FreeRTOS task that periodically updates the broadcasted BPM value
    // Stack size: 2048 words (~8KB), priority: 5 (medium priority for regular updates)
    xTaskCreate(update_broadcast_char_value_task, "Update Broadcast Char Value Task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "BLE initialized");
}