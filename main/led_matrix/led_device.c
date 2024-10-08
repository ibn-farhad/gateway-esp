/*
 * led_device.c
 *
 *  Created on: Sep 30, 2023
 *      Author: markscheider
 */
#include "led_device.h"
#ifdef CONFIG_LED_MATRIX

static const char *TAG = "LED";

#define BUF_SIZE (512)

enum Commands {
	GET_STATUS,
	PRINT_TEXT,

	STATUS = 100
};

enum LEDStatus{
	ONLINE,
	DEGRADED,
	OFFLINE

};

SemaphoreHandle_t task_sem;

int LEDDeviceStatus = OFFLINE;
// Configure a temporary buffer for the incoming data
static char data[BUF_SIZE] = {0};
// barcode scanner callback function
cb LED_cb;


static void LEDMatrixCbTask(void *arg){

	char data_cb[128] = {0};
	memset(data_cb, 0, 128);        // Read data from the UART
	int len = uart_read_bytes(CONFIG_LED_UART_PORT_NUM, data_cb, (BUF_SIZE - 1), 5);
	if (len) {

		const cJSON *command = NULL;
		cJSON *data_json = cJSON_Parse(data_cb);
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
			printf("LEDMatrixCbTask Checking LED Device json \"%d\"\n", command->valueint);
			switch (command->valueint)
			{
				case STATUS:
				{
					cJSON *status = cJSON_GetObjectItemCaseSensitive(data_json, "status");
					if(cJSON_IsNumber(status)){
						switch (status->valueint) {
							case 0:
								LEDDeviceStatus = ONLINE;
								break;
							case 1:
							{
								cJSON *status_text = cJSON_GetObjectItemCaseSensitive(data_json, "status_text");
								LEDDeviceStatus =  ONLINE;
								ESP_LOGW(TAG, "display JSON pars error : %s", status_text->string);
								break;
							}
							default:
								break;
						}

					}
					break;
				}
			}
		}
	}
	vTaskDelete(NULL);
}
void LEDHandleCommand(char *str, httpd_handle_t handle, int fd){

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
		printf("LEDMatrixCbTask Checking LED Device json \"%d\"\n", command->valueint);
		switch (command->valueint)
		{
			case GET_STATUS:
			{
				cJSON *command_res = NULL;
				cJSON *status = NULL;
				cJSON *response = cJSON_CreateObject();
				if (response == NULL)
				{
					return;
				}

				command_res = cJSON_CreateNumber(STATUS);
				if (command_res == NULL)
				{
					return;
				}
				/* after creation was successful, immediately add it to the monitor,
				 * thereby transferring ownership of the pointer to it */
				cJSON_AddItemToObject(response, "command", command_res);

				status = cJSON_CreateNumber(LEDDeviceStatus);
				if (status == NULL)
				{
					return;
				}
				/* after creation was successful, immediately add it to the monitor,
				 * thereby transferring ownership of the pointer to it */
				cJSON_AddItemToObject(response, "status", status);
				LED_cb(cJSON_Print(response), handle, fd);
				break;
			}

			case PRINT_TEXT:
			{
				ESP_LOGI(TAG, "str len : %d", strlen(str));
				xSemaphoreTake(task_sem, portMAX_DELAY);
				uart_write_bytes(CONFIG_LED_UART_PORT_NUM, str, strlen(str));
				xTaskCreate(LEDMatrixCbTask, "LEDMatrixCbTask", CONFIG_LED_TASK_STACK_SIZE, NULL, 12, NULL);
				xSemaphoreGive(task_sem);
				break;
			}
			default:
				break;
		}
	}


}

static void LEDMatrixTask(void *arg){

	while(1)
	{
		xSemaphoreTake(task_sem, portMAX_DELAY);
		cJSON *command = NULL;
		cJSON *request = cJSON_CreateObject();
	//	if (request == NULL)
	//	{
	//		return;
	//	}

		command = cJSON_CreateNumber(GET_STATUS);
	//	if (command == NULL)
	//	{
	//		return;
	//	}
		/* after creation was successful, immediately add it to the monitor,
		 * thereby transferring ownership of the pointer to it */
		cJSON_AddItemToObject(request, "command", command);
		printf("json:%s || len: %d\n",cJSON_Print(request), strlen(cJSON_Print(request)));
		uart_write_bytes(CONFIG_LED_UART_PORT_NUM, cJSON_Print(request), strlen(cJSON_Print(request)));

		vTaskDelay(10);
		memset(data, 0, BUF_SIZE);        // Read data from the UART
		int len = uart_read_bytes(CONFIG_LED_UART_PORT_NUM, data, (BUF_SIZE - 1), 1);
		printf("resd bytes  : \"%s\"len : %d\n", data, len);
		if (len) {
			const cJSON *command = NULL;
			cJSON *data_json = cJSON_Parse(data);
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
//				printf("LEDMatrixTask Checking LED json \"%d\"\n", command->valueint);
				switch (command->valueint)
				{
					case STATUS:
					{
						cJSON *status = cJSON_GetObjectItemCaseSensitive(data_json, "status");
						if(cJSON_IsNumber(status)){
							switch (status->valueint) {
								case 0:
									LEDDeviceStatus =  ONLINE;
									break;
	//							case 1:
	//							{
	//								cJSON *status_text = cJSON_GetObjectItemCaseSensitive(data_json, "status_text");
	//								LEDDeviceStatus =  ONLINE;
	//								ESP_LOGW(TAG, "display JSON pars error : %s", status_text->string);
	//								break;
	//							}
								default:
									break;
							}

						}
						break;
					}
					default:
						LEDDeviceStatus = OFFLINE;
						break;
				}
			}
		}
		else
		{
			LEDDeviceStatus = OFFLINE;
		}
		xSemaphoreGive(task_sem);
		vTaskDelay(200);
	}
	vTaskDelete(NULL);

}
void initLEDDevice(cb callback){
	LED_cb = callback;
	task_sem = xSemaphoreCreateBinary();
	xSemaphoreGive(task_sem);

	    /* Configure parameters of an UART driver,
	     * communication pins and install the driver */
	    uart_config_t uart_config = {
	        .baud_rate = CONFIG_LED_UART_BAUD_RATE,
	        .data_bits = UART_DATA_8_BITS,
	        .parity    = UART_PARITY_DISABLE,
	        .stop_bits = UART_STOP_BITS_1,
	        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
	        .source_clk = UART_SCLK_DEFAULT,
	    };
	    int intr_alloc_flags = 0;

	#if CONFIG_UART_ISR_IN_IRAM
	    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
	#endif

	    ESP_ERROR_CHECK(uart_driver_install(CONFIG_LED_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
	    ESP_ERROR_CHECK(uart_param_config(CONFIG_LED_UART_PORT_NUM, &uart_config));
	    ESP_ERROR_CHECK(uart_set_pin(CONFIG_LED_UART_PORT_NUM, CONFIG_LED_UART_TXD, CONFIG_LED_UART_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

	    // TODO: status might be checked not there
	    LEDDeviceStatus = ONLINE;

	    xTaskCreate(LEDMatrixTask, "LEDTask", CONFIG_LED_TASK_STACK_SIZE, NULL, 11, NULL);
}

#endif

