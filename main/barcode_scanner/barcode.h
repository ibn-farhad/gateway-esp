/*
 * barcode.h
 *
 *  Created on: Aug 18, 2023
 *      Author: markscheider
 */

#ifndef MAIN_BARCODE_SCANNER_BARCODE_H_
#define MAIN_BARCODE_SCANNER_BARCODE_H_
#include "sdkconfig.h"

#ifdef CONFIG_BARCODE_SCANNER

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "esp_err.h"
#include <wiegand.h>



#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>


typedef void (*callb)(char*, httpd_handle_t, int);

void initBarcodeScanner(callb callback);
void barcodeHandleCommand(char *str, httpd_handle_t handle, int fd);
static void reader_callback(wiegand_reader_t *r);

#endif /* BARCODE_SCANNER */

#endif /* MAIN_BARCODE_SCANNER_BARCODE_H_ */
