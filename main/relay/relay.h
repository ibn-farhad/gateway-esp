/*
 * relay.h
 *
 *  Created on: Aug 20, 2023
 *      Author: markscheider
 */

#ifndef MAIN_RELAY_RELAY_H_
#define MAIN_RELAY_RELAY_H_

#include "sdkconfig.h"

#ifdef CONFIG_RELAY
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "esp_log.h"


#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#define RELAY_PIN	CONFIG_RELAY_PIN
#define RELAY_PIN_SEL		(1ULL<<RELAY_PIN)
#ifdef CONFIG_RELAY_INVERSE_CONTROL
#define RELAY_INVERSE_CONTROL CONFIG_RELAY_INVERSE_CONTROL
#else
#define RELAY_INVERSE_CONTROL 0
#endif

typedef void (*callb)(char*, httpd_handle_t, int);

void initRelay(callb callback);
void relayHandleCommand(char *str, httpd_handle_t handle, int fd);

#endif /* CONFIG_RELAY */

#endif /* MAIN_RELAY_RELAY_H_ */
