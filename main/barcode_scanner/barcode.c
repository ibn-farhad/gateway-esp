/*
 * barcode.c
 *
 *  Created on: Aug 18, 2023
 *      Author: markscheider
 */
#include "barcode.h"
#include "sdkconfig.h"
#ifdef CONFIG_BARCODE_SCANNER

/**
 * This is an example which echos any data it receives on configured UART back to the sender,
 * with hardware flow control turned off. It does not use UART driver event queue.
 *
 * - Port: configured UART
 * - Receive (Rx) buffer: on
 * - Transmit (Tx) buffer: off
 * - Flow control: off
 * - Event queue: off
 * - Pin assignment: see defines below (See Kconfig)
 */
#ifndef CONFIG_WIEGAND

#define ECHO_TEST_TXD (CONFIG_EXAMPLE_UART_TXD)
#define ECHO_TEST_RXD (CONFIG_EXAMPLE_UART_RXD)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM      (CONFIG_EXAMPLE_UART_PORT_NUM)
#define ECHO_UART_BAUD_RATE     (CONFIG_EXAMPLE_UART_BAUD_RATE)
#define ECHO_TASK_STACK_SIZE    (CONFIG_EXAMPLE_TASK_STACK_SIZE)

#elif CONFIG_WIEGAND
#define WIEGAND_D0 				CONFIG_EXAMPLE_D0_GPIO
#define WIEGAND_D1 				CONFIG_EXAMPLE_D1_GPIO
#define WIEGAND_BUFF_SIZE 		CONFIG_EXAMPLE_BUF_SIZE

static wiegand_reader_t reader;
static QueueHandle_t queue = NULL;

// Single data packet
typedef struct
{
    uint8_t data[CONFIG_EXAMPLE_BUF_SIZE];
    size_t bits;
} data_packet_t;

// callback on new data in reader
static void reader_callback(wiegand_reader_t *r)
{
    // you can decode raw data from reader buffer here, but remember:
    // reader will ignore any new incoming data while executing callback

    // create simple undecoded data packet
    data_packet_t p;
    p.bits = r->bits;
    memcpy(p.data, r->buf, WIEGAND_BUFF_SIZE);

    // Send it to the queue
    xQueueSendToBack(queue, &p, 0);
}

#endif

static const char *TAG = "BARCODE_SCANNER";

#define BUF_SIZE (512)

enum Commands {
	GET_STATUS,

	STATUS = 100,
	BARCODE
};

enum BarcodeStatus{
	ONLINE,
	DEGRADED,
	OFFLINE

};
int barcodeStatus = OFFLINE;
// Configure a temporary buffer for the incoming data
static char data[BUF_SIZE] = {0};
// barcode scanner callback function
callb barcodeScanner_cb;
int len = 0;
void barcodeHandleCommand(char *str, httpd_handle_t handle, int fd){
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
		printf("Checking barcode json \"%d\"\n", command->valueint);

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

				status = cJSON_CreateNumber(barcodeStatus);
				if (status == NULL)
				{
					return;
				}
				/* after creation was successful, immediately add it to the monitor,
				 * thereby transferring ownership of the pointer to it */
				cJSON_AddItemToObject(response, "status", status);


				barcodeScanner_cb(cJSON_Print(response), handle, fd);
				cJSON_Delete(response);
				break;
			}
			default:
				break;
		}
	}
	cJSON_Delete(data_json);
}

static void barcodeScannerTask(void *arg)
{
#ifdef CONFIG_WIEGAND
	data_packet_t p;
#endif
    while (1) {
    	memset(data, 0, BUF_SIZE);        // Read data from the UART

#ifndef CONFIG_WIEGAND
        len = uart_read_bytes(ECHO_UART_PORT_NUM, data, (BUF_SIZE - 1), 10);
#elif CONFIG_WIEGAND

        ESP_LOGI(TAG, "Waiting for Wiegand data...");
		xQueueReceive(queue, &p, portMAX_DELAY);
		int bytes = p.bits / 8;
		int tail = p.bits % 8;
		printf("==========================================\n");
		printf("Bits received: %d\n", p.bits);
		printf("Received data:");

		for (size_t i = 0; i < bytes + (tail ? 1 : 0); i++)
			printf(" 0x%02x", p.data[i]);
		printf("\n==========================================\n");
		uint32_t ID = (uint32_t)((p.data[0] << 24) + (p.data[1] << 16) + (p.data[2] << 8) + p.data[3]);
		ID = (ID << 1) >> 8;
		printf("%lu\n\r", ID);
		sprintf(data, "%lu",ID );
		len = bytes;
//		mbedtls_base64_encode(data,BUF_SIZE, (size_t*)&len,(const unsigned char *) p.data, bytes + tail);
#endif

        if (len) {
//            data[len] = '\0';
            cJSON *command = NULL;
			cJSON *dataScanned = NULL;
			cJSON *response = cJSON_CreateObject();
			if (response == NULL)
			{
				return;
			}

			command = cJSON_CreateNumber(BARCODE);
			if (command == NULL)
			{
				return;
			}
			/* after creation was successful, immediately add it to the monitor,
			 * thereby transferring ownership of the pointer to it */
			cJSON_AddItemToObject(response, "command", command);

			dataScanned = cJSON_CreateString((const char*)data);
			if (dataScanned == NULL)
			{
				return;
			}
			/* after creation was successful, immediately add it to the monitor,
			 * thereby transferring ownership of the pointer to it */
			cJSON_AddItemToObject(response, "data", dataScanned);

			barcodeScanner_cb(cJSON_Print(response), NULL, 2);

            ESP_LOGI(TAG, "RFID : %s", (char *) data);
//            ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);

            cJSON_Delete(response);
        }
        else if (len == -1) {
#ifndef CONFIG_WIEGAND
        	uart_flush(ECHO_UART_PORT_NUM);
#endif
		}
        vTaskDelay(2);
    }



}

void initBarcodeScanner(callb callback)
{
	barcodeScanner_cb = callback;
	barcodeStatus = ONLINE;

#ifndef CONFIG_WIEGAND
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
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

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));



    xTaskCreate(barcodeScannerTask, "barcodeScannerTask", 8192, NULL, 2, NULL);

#elif CONFIG_WIEGAND
    queue = xQueueCreate(5, sizeof(data_packet_t));
        if (!queue)
        {
            ESP_LOGE(TAG, "Error creating queue");
            ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
        }

        // Initialize reader
        ESP_ERROR_CHECK(wiegand_reader_init(&reader, WIEGAND_D0, WIEGAND_D1,
                1, WIEGAND_BUFF_SIZE, reader_callback, WIEGAND_MSB_FIRST, WIEGAND_LSB_FIRST));

        xTaskCreate(barcodeScannerTask, TAG, 8192, NULL, 5, NULL);
#endif
}
#endif
