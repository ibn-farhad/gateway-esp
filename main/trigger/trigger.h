/*
 * trigger.h
 *
 *  Created on: Aug 20, 2023
 *      Author: markscheider
 */

#ifndef MAIN_TRIGGER_TRIGGER_H_
#define MAIN_TRIGGER_TRIGGER_H_

#include "sdkconfig.h"

#ifdef CONFIG_TRIGGER

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

#define TRIGGER_PIN	 	CONFIG_TRIGGER_PIN
#define TRIGGER_PIN_SEL		(1ULL<<TRIGGER_PIN)


typedef void (*callb)(char*, httpd_handle_t, int);

void initTrigger(callb callback);
void triggerHandleCommand(char *str, httpd_handle_t handle, int fd);


#endif /* CONFIG_TRIGGER */


#endif /* MAIN_TRIGGER_TRIGGER_H_ */
