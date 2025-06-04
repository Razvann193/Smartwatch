#ifndef SENSOR_H
#define SENSOR_H

void sensor_task(void *pvParameters);
void get_bpm_data(int *buffer, int *size);
void clear_bpm_data();

#endif