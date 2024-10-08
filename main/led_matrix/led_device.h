/*
 * led_device.h
 *
 *  Created on: Sep 30, 2023
 *      Author: markscheider
 */

#ifndef MAIN_LED_MATRIX_LED_DEVICE_H_
#define MAIN_LED_MATRIX_LED_DEVICE_H_
#include "sdkconfig.h"

#ifdef CONFIG_BARCODE_SCANNER

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "esp_log.h"


#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>


typedef void (*cb)(char*, httpd_handle_t, int);

void initLEDDevice(cb callback);
void LEDHandleCommand(char *str, httpd_handle_t handle, int fd);

#endif /* LED Matrix Device */
#endif /* MAIN_LED_MATRIX_LED_DEVICE_H_ */
