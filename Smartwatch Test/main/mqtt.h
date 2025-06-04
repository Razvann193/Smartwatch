#ifndef MQTT_H
#define MQTT_H

extern int global_bpm; // Declare the global BPM variable

void mqtt_init();
void data_send_task(void *pvParameters);
void wifi_init();
void random_bpm_task(void *pvParameters);

#endif