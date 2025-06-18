#ifndef BLE_H
#define BLE_H

// #include "esp_gap_ble_api.h" // GAP BLE API
// #include "esp_gatts_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// Function to initialize BLE
void ble_init();

// Function to send data over BLE
void ble_send_data();

#endif // BLE_H
