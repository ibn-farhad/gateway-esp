 /*
 * trigger.c
 *
 *  Created on: Aug 20, 2023
 *      Author: markscheider
 */
#include "trigger.h"
#ifdef CONFIG_TRIGGER

callb trigger_cb;

enum Commands {
	GET_STATUS,

	STATUS = 100,
	IMPULSE
};

enum TriggerStatus{
	ONLINE,
	DEGRADED,
	OFFLINE
};

bool triggerStat = false;
int triggerStatus = OFFLINE;

QueueHandle_t interputQueue;

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    xQueueSendFromISR(interputQueue, &pinNumber, NULL);
}

void triggerHandleCommand(char *str, httpd_handle_t handle, int fd){
	/*
	 * getting barcode scanner status
	 */
	const cJSON *command = NULL;
	cJSON *data_json = cJSON_Parse(str);
	if (data_json == NULL)
	{
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL)
		{
			printf("Error before: \n");
		}
	}
	command = cJSON_GetObjectItemCaseSensitive(data_json, "command");
	if ( cJSON_IsNumber(command))
	{
		printf("Checking trigger json \"%d\"\n", command->valueint);

		switch (command->valueint)
		{
			case GET_STATUS:
			{
				cJSON *command = NULL;
				cJSON *status = NULL;
				cJSON *response = cJSON_CreateObject();
				if (response == NULL)
				{
					return;
				}
				command = cJSON_CreateNumber(STATUS);
				if (command == NULL)
				{
					return;
				}
				/* after creation was successful, immediately add it to the monitor,
				 * thereby transferring ownership of the pointer to it */
				cJSON_AddItemToObject(response, "command", command);

				status = cJSON_CreateNumber(triggerStatus);
				if (status == NULL)
				{
					return;
				}
				/* after creation was successful, immediately add it to the monitor,
				 * thereby transferring ownership of the pointer to it */
				cJSON_AddItemToObject(response, "status", status);


				trigger_cb(cJSON_Print(response), handle, fd);
				cJSON_Delete(response);
				break;
			}
			default:
				break;
		}
	}
	cJSON_Delete(data_json);
}

void triggerTask(){
	int pinNumber;
	while(1){
//		if(!gpio_get_level(TRIGGER_PIN) && !triggerStat){
		if (xQueueReceive(interputQueue, &pinNumber, portMAX_DELAY)){
			ESP_LOGI("INPUT_TASK", "triggered");

			cJSON *command = NULL;
			cJSON *response = cJSON_CreateObject();
			if (response == NULL)
			{
				return;
			}

			command = cJSON_CreateNumber(IMPULSE);
			if (command == NULL)
			{
				return;
			}
			/* after creation was successful, immediately add it to the monitor,
			 * thereby transferring ownership of the pointer to it */
			cJSON_AddItemToObject(response, "command", command);

			trigger_cb(cJSON_Print(response), NULL, 4);
			triggerStat = true;

			cJSON_Delete(response);
		}
		else if (gpio_get_level(TRIGGER_PIN)) {
			triggerStat = false;
		}
		vTaskDelay(10);
	}
	vTaskDelete(NULL);
}
void initTrigger(callb callback){

	trigger_cb = callback;

	gpio_config_t trigger_config = {
			.intr_type = GPIO_INTR_NEGEDGE,
			.mode = GPIO_MODE_INPUT,
			.pin_bit_mask = TRIGGER_PIN_SEL,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.pull_up_en = GPIO_PULLUP_ENABLE
	};

	gpio_reset_pin(TRIGGER_PIN);
	gpio_config(&trigger_config);
	 gpio_set_intr_type(TRIGGER_PIN, GPIO_INTR_NEGEDGE);
//	gpio_intr_enable(TRIGGER_PIN);
	triggerStatus = ONLINE;

//	xTaskCreate(triggerTask, "triggerTask", 4096, NULL, 2, NULL);

	interputQueue = xQueueCreate(10, sizeof(int));
	xTaskCreate(triggerTask, "triggerTask", 4096, NULL, 5, NULL);

	gpio_install_isr_service(0);
	gpio_isr_handler_add(TRIGGER_PIN, gpio_interrupt_handler, (void *)TRIGGER_PIN);
}
#endif
