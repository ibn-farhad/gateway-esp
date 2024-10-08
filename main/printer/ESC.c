/*
 * printer.c
 *
 *  Created on: Aug 9, 2023
 *      Author: markscheider
 */
#include "ESC.h"

static const char *CLASS_TAG = "CLASS";
static const char *DAEMON = "DAEMON";

usb_host_lib_info_t info_ret;
QueueHandle_t xQueue;

SemaphoreHandle_t callback_sem;
SemaphoreHandle_t printingQueue_sem;

usb_transfer_t *transfer;
int transfer_index = 0;

int endPointAddressIn = 0x82;
int endPointAddressOut = 0x01;
unsigned char buff[30000] = {0};

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    uint32_t actions;
} class_driver_t;

struct class_driver_control {
    uint32_t actions;
    uint8_t dev_addr;
    usb_host_client_handle_t client_hdl;
    usb_device_handle_t dev_hdl;
};
enum Commands {
	GET_STATUS,
	PRINT,

	STATUS = 100,
	DATA
};

enum PrinteStatus{
	ONLINE,
	DEGRADED,
	OFFLINE

};
uint8_t statusCode = 0;
uint8_t printerStatus = OFFLINE;
char *printerStatusString;
char platenIsOpen[] = "Platen is opened";
char noPaper[] = "Printing is being stopped - No paper";
char feedButtonPressed[] = "Paper is being fed by FEED button";
char mechanicalError[] = "Mechanical error has occurred";
char autoCutterError[] = "Auto Cutter Error Occurred";
char unrecoverableError[] = "Unrecoverable Error has occurred";
char autoRecoverableError[] = "Auto Recoverable ErrorOccurred";

callb escCallback;

uint8_t printMode,
      prevByte,      // Last character issued to printer
      column,        // Last horizontal column printed
      maxColumn,     // Page width (output 'wraps' at this point)
      charHeight,    // Height of characters, in 'dots'
      lineSpacing,   // Inter-line spacing (not line height), in dots
      barcodeHeight, // Barcode height in dots, not including text
      maxChunkHeight,
      dtrPin;

SemaphoreHandle_t transfer_cb_sem;

void initESC(callb callback){
	escCallback = callback;

	SemaphoreHandle_t signaling_sem = xSemaphoreCreateBinary();
	transfer_cb_sem = xSemaphoreCreateBinary();
	xSemaphoreGive(transfer_cb_sem);
	xSemaphoreTake(transfer_cb_sem, 1);

	TaskHandle_t daemon_task_hdl;
	TaskHandle_t class_driver_task_hdl;

	//Create daemon task
	xTaskCreatePinnedToCore(host_lib_daemon_task,
						   "daemon",
						   4096,
						   (void *)signaling_sem,
						   DAEMON_TASK_PRIORITY,
						   &daemon_task_hdl,
						   0);
	//Create the class driver task
	xTaskCreatePinnedToCore(class_driver_task,
						   "class",
						   4096,
						   (void *)signaling_sem,
						   CLASS_TASK_PRIORITY,
						   &class_driver_task_hdl,
						   0);
	// Create a task that periodically gets a printer status
	xTaskCreate(getPrinterStatTask,
				"BulkIn",
				4096,
				NULL,
				1,
				NULL);


}

void escHandleCommand(char *str, httpd_handle_t handle, int fd)
{
	xSemaphoreTake(printingQueue_sem, portMAX_DELAY);
	xSemaphoreTake(callback_sem, 90);

	const cJSON *command = NULL;
	cJSON *data_json = cJSON_Parse(str);
	if (data_json == NULL)
	{
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL)
		{
			fprintf(stderr, "Error before: \n");
		}
	}

	command = cJSON_GetObjectItemCaseSensitive(data_json, "command");
	if ( cJSON_IsNumber(command))
	{
		printf("printer command \"%d\"\n", command->valueint);

		switch (command->valueint)
		{
			case GET_STATUS:
			{
				cJSON *command_status = NULL;
				cJSON *status = NULL;
				cJSON *status_text = NULL;
				cJSON *response = cJSON_CreateObject();
				if (response == NULL)
				{
					return;
				}

				command_status = cJSON_CreateNumber(STATUS);
				if (command_status == NULL)
				{
					return;
				}
				/* after creation was successful, immediately add it to the monitor,
				 * thereby transferring ownership of the pointer to it */
				cJSON_AddItemToObject(response, "command", command_status);

				status = cJSON_CreateNumber(printerStatus);
				if (status == NULL)
				{
					return;
				}
				/* after creation was successful, immediately add it to the monitor,
				 * thereby transferring ownership of the pointer to it */
				cJSON_AddItemToObject(response, "status", status);

				if(printerStatus == DEGRADED){
					status_text = cJSON_CreateString(printerStatusString);
					cJSON_AddItemToObject(response, "status_text", status_text);
				}

				escCallback(cJSON_Print(response), handle, fd);
				cJSON_Delete(response);
				break;
			}
			case PRINT:
			{

				ESP_LOGI("handler","before (getPrinterStat(1) == 22)");
				if (getPrinterStat(1) == 22)//(printerStatus == ONLINE)//(getPrinterStat(1) == 22)
				{
					ESP_LOGI("handler","after (getPrinterStat(1) == 22)");
					const cJSON *jsonData = NULL;
					const cJSON *printing_param = NULL;
					const cJSON *feed_s = NULL;

					usb_host_transfer_alloc(20480, 0, &transfer);
					memset(transfer->data_buffer, 0x00, 20480);
					transfer_index = 0;
					printing_param = cJSON_GetObjectItemCaseSensitive(data_json, "data");

					int arraySize = cJSON_GetArraySize(printing_param);

					for(int i = 0; i < arraySize; i ++)
					{
						jsonData = cJSON_GetArrayItem(printing_param, i);

						if(cJSON_HasObjectItem(jsonData, "text"))
						{

							cJSON *text = cJSON_GetObjectItemCaseSensitive(jsonData, "text");
							cJSON *alignment = cJSON_GetObjectItemCaseSensitive(jsonData, "alignment");
							cJSON *size = cJSON_GetObjectItemCaseSensitive(jsonData, "size");
							cJSON *bold = cJSON_GetObjectItemCaseSensitive(jsonData, "bold");
							cJSON *underline = cJSON_GetObjectItemCaseSensitive(jsonData, "underline");
							cJSON *line_spacing = cJSON_GetObjectItemCaseSensitive(jsonData, "line-spacing");
								   feed_s = cJSON_GetObjectItemCaseSensitive(jsonData, "feed");


							if (cJSON_IsString(text) && (text->valuestring != NULL))
							{
//								printf("text : \"%s\"\n", text->valuestring);
								printf(" ");
							}

							if(!cJSON_IsString(alignment)){
								alignment = (cJSON*)malloc(sizeof(cJSON));
								alignment->valuestring = "left";
							}

							if(!cJSON_IsString(size)){
								size = (cJSON*)malloc(sizeof(cJSON));
								size->valuestring = "S";
							}

							if(!cJSON_IsBool(bold)){
								bold = (cJSON*)malloc(sizeof(cJSON));
								bold->valueint = 0;
							}

							if(!cJSON_IsBool(underline)){
								underline = (cJSON*)malloc(sizeof(cJSON));
								underline->valueint = 0;
							}

							if(!cJSON_IsNumber(line_spacing)){
								line_spacing = (cJSON*)malloc(sizeof(cJSON));
								line_spacing->valueint = 75;
							}
							ESP_LOGI("handlet_text","before print_text");
							print_text(text->valuestring, size->valuestring, (bool)bold->valueint, (bool)underline->valueint, alignment->valuestring, line_spacing->valueint, 'A');

//							cJSON_Delete(text);
//							cJSON_Delete(alignment);
//							cJSON_Delete(size);
//							cJSON_Delete(bold);
//							cJSON_Delete(underline);
//							cJSON_Delete(line_spacing);

							ESP_LOGI("handlet_text","after print_text");
						}

						if(cJSON_HasObjectItem(jsonData, "image"))
						{
							printf("JSON has image\n");
							cJSON *image = 		cJSON_GetObjectItemCaseSensitive(jsonData, "image");
							cJSON *alignment = 	cJSON_GetObjectItemCaseSensitive(jsonData, "alignment");
							cJSON *width = 		cJSON_GetObjectItemCaseSensitive(jsonData, "width");
							cJSON *height = 	cJSON_GetObjectItemCaseSensitive(jsonData, "height");

							if(!cJSON_IsString(alignment)){
								alignment = (cJSON*)malloc(sizeof(cJSON));
								alignment->valuestring = "center";
							}
							if(!cJSON_IsNumber(width)){
								width = (cJSON*)malloc(sizeof(cJSON));
								width->valueint = 248;
							}
							if(!cJSON_IsNumber(height)){
								height = (cJSON*)malloc(sizeof(cJSON));
								height->valueint = 248;
							}
							size_t outlen;

							mbedtls_base64_decode(buff, 20000,&outlen,
									(const unsigned  char*)image->valuestring, strlen(image->valuestring));
							printf("inLen : %d, outLen : %d\n\r", strlen(image->valuestring), outlen);
							printf("width : %d, height: %d\n",width->valueint, height->valueint);
							print_image(width->valueint, height->valueint,
									alignment->valuestring, buff);

//							cJSON_Delete(image);
//							cJSON_Delete(alignment);
//							cJSON_Delete(width);
//							cJSON_Delete(height);
						}
						if(cJSON_HasObjectItem(jsonData, "barcode"))
						{
							printf("JSON has barcode\n");
							cJSON *barcode = 	cJSON_GetObjectItemCaseSensitive(jsonData, "barcode");
							cJSON *alignment = 	cJSON_GetObjectItemCaseSensitive(jsonData, "alignment");
							cJSON *height = 	cJSON_GetObjectItemCaseSensitive(jsonData, "height");
							cJSON *type = 		cJSON_GetObjectItemCaseSensitive(jsonData, "type");

							if(!cJSON_IsString(alignment)){
								alignment = (cJSON*)malloc(sizeof(cJSON));
								alignment->valuestring = "center";
							}

							if(!cJSON_IsNumber(height)){
								height = (cJSON*)malloc(sizeof(cJSON));
								height->valueint = 200;
							}

							if(!cJSON_IsNumber(type)){
								type = (cJSON*)malloc(sizeof(cJSON));
								type->valueint = 4;
							}

							setBarcodeHeight(height->valueint);
							printBarcode(barcode->valuestring, type->valueint);

						}
					}
					if(cJSON_IsNumber(feed_s)){
						if( feed_s->valueint != NULL)
							feed(feed_s->valueint);
					}
					else
						feed(5);
					partial_cut();

					print();
				}
				break;
			}
			default:
				break;
		}
	}
//	if((data_json->type & cJSON_IsReference) > 0)
//	printf("data_json -> type \"%d\"\n", data_json->type);
	cJSON_Delete(data_json);
//	printf("data_json -> type \"%d\"\n", data_json->type);
	xSemaphoreGive(callback_sem);
	xSemaphoreGive(printingQueue_sem);
}

void host_lib_daemon_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;

    ESP_LOGI(DAEMON, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));

	//Signal to the class driver task that the host library is installed
    xSemaphoreGive(signaling_sem);
    vTaskDelay(10); //Short delay to let client task spin up

    while(1){
    	bool has_clients = true;
		bool has_devices = true;
		while (has_clients || has_devices) {
			usb_host_lib_info(&info_ret);
			ESP_LOGI(DAEMON, "has_clients : %d,  has_devices : %d", info_ret.num_clients, info_ret.num_devices);

			ESP_LOGI(DAEMON, "usb_host_lib_handle_events");
			uint32_t event_flags;
			ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
			ESP_LOGI(DAEMON, "usb_host_lib_handle_events after event_flags : %lu", event_flags);
			if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
				has_clients = false;
			}
			if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
				has_devices = false;
			}
		}
		ESP_LOGI(CLASS_TAG, "No more clients and devices");

		//Uninstall the USB Host Library
		ESP_ERROR_CHECK(usb_host_uninstall());
		vTaskDelay(10);

		// Reinstal the usb host Library
		xSemaphoreTake(signaling_sem, portMAX_DELAY);
		ESP_ERROR_CHECK(usb_host_install(&host_config));
		xSemaphoreGive(signaling_sem);

    }
    //Wait to be deleted
    xSemaphoreGive(signaling_sem);
    ESP_LOGI(DAEMON,"%s task suspended\n", CLASS_TAG);
    vTaskSuspend(NULL);
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
	 //This is function is called from within usb_host_client_handle_events(). Don't block and try to keep it short
	    struct class_driver_control *class_driver_obj = (struct class_driver_control *)arg;
	    ESP_LOGI(CLASS_TAG, "event_msg : %d", event_msg->event);
	    switch (event_msg->event) {
	        case USB_HOST_CLIENT_EVENT_NEW_DEV:
	            class_driver_obj->actions = CLASS_DRIVER_ACTION_OPEN_DEV;
	            class_driver_obj->dev_addr = event_msg->new_dev.address; //Store the address of the new device
	            printerStatus = ONLINE;
	            break;
	        case USB_HOST_CLIENT_EVENT_DEV_GONE:
	            class_driver_obj->actions = CLASS_DRIVER_ACTION_CLOSE_DEV;
	            printerStatus = OFFLINE;
	            break;
	        default:
	            break;
	    }
}


static void action_get_info(struct class_driver_control *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(CLASS_TAG, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    ESP_LOGI(CLASS_TAG, "\t%s speed", (dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full");
    ESP_LOGI(CLASS_TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);
    //Todo: Print string descriptors

    //Get the device descriptor next

}

static void action_get_dev_desc(struct class_driver_control *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(CLASS_TAG, "Getting device descriptor");
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    //Get the device's config descriptor next
}

static void action_get_config_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);

    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));

    int offset = 0;
    uint16_t wTotalLength = config_desc->wTotalLength;
    const usb_standard_desc_t *next_desc = (const usb_standard_desc_t *)config_desc;
    do {
		switch (next_desc->bDescriptorType) {

			case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
				const usb_ep_desc_t *ep_desc = ((const usb_ep_desc_t *)next_desc);
				ESP_LOGI("TEST","0x%x: %d %s", ep_desc->bEndpointAddress,
						USB_EP_DESC_GET_EP_DIR(ep_desc),
						USB_EP_DESC_GET_EP_DIR(ep_desc) ? "IN" : "OUT");
				if(USB_EP_DESC_GET_EP_DIR(ep_desc)){
					endPointAddressIn = ep_desc->bEndpointAddress;
				}
				else
				{
					endPointAddressOut = ep_desc->bEndpointAddress;
				}

				break;

			default:
				break;
		}

		next_desc = usb_parse_next_descriptor(next_desc, wTotalLength, &offset);

	} while (next_desc != NULL);

    ESP_LOGI("TEST","IN : 0x%x;  OUT : 0x%x", endPointAddressIn, endPointAddressOut);
}

static void action_get_str_desc(struct class_driver_control *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    if (dev_info.str_desc_manufacturer) {
        ESP_LOGI(CLASS_TAG, "Getting Manufacturer string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        ESP_LOGI(CLASS_TAG, "Getting Product string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        ESP_LOGI(CLASS_TAG, "Getting Serial Number string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    //Nothing to do until the device disconnects
}

static void transfer_cb(usb_transfer_t *transfer)
{
    //This is function is called from within usb_host_client_handle_events(). Don't block and try to keep it short
    ESP_LOGI("transfer_cb","Transfer status %d, buf_len %d, actual number of bytes transferred %d\n", transfer->status, transfer->num_bytes,transfer->actual_num_bytes);
    if (transfer->status != 0) {
    	char* p;
    	p = (char*)transfer->data_buffer;
    	printf("%s", p);
	}
    xSemaphoreGive(transfer_cb_sem);
}

static void getStatus_cb(usb_transfer_t *transfer_status)
{
    if(transfer_status->bEndpointAddress == endPointAddressIn){
    	statusCode = transfer_status->data_buffer[0];
    }
    usb_host_transfer_free(transfer_status);
    xSemaphoreGive(callback_sem);
}

void class_driver_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;

    ESP_LOGI(CLASS_TAG, "Registering Client");
    struct class_driver_control class_driver_obj = {0};
    struct class_driver_control *pvclass_driver_obj;
    class_driver_t driver_obj = {0};

    callback_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(callback_sem);

    printingQueue_sem = xSemaphoreCreateBinary();
	xSemaphoreGive(printingQueue_sem);

    // creating Queue
    xQueue = xQueueCreate( 2, sizeof( struct class_driver_control * ) );

    //Wait until daemon task has installed USB Host Library
    xSemaphoreTake(signaling_sem, portMAX_DELAY);
	//Register the client
	usb_host_client_config_t client_config = {
		.is_synchronous = false,
		.max_num_event_msg = 5,
		.async = {
			.client_event_callback = client_event_cb,
			.callback_arg = &class_driver_obj,
		}
	};

	if( xQueue == 0 )
	{
	 // Failed to create the queue.
		ESP_LOGI(CLASS_TAG, "xQueue is failed to create!");
	}
	usb_host_client_register(&client_config, &class_driver_obj.client_hdl);

	while(1){

		//Event handling loop
		bool exit = false;
		while (!exit) {
//			ESP_LOGI(CLASS_TAG, "usb_host_client_handle_events");
			//Call the client event handler function
			usb_host_client_handle_events(class_driver_obj.client_hdl, portMAX_DELAY);
			//Execute pending class driver actions

			if (class_driver_obj.actions & CLASS_DRIVER_ACTION_OPEN_DEV) {
				driver_obj.actions = class_driver_obj.actions;
				driver_obj.client_hdl = class_driver_obj.client_hdl;
				driver_obj.dev_addr = class_driver_obj.dev_addr;
				driver_obj.dev_hdl = class_driver_obj.dev_hdl;
//				action_get_config_desc(&driver_obj);
				//Open the device and claim interface 1
				ESP_LOGI(CLASS_TAG, "class_driver_obj.actions : %lx", class_driver_obj.actions);
				ESP_LOGI(CLASS_TAG,"device open : %d", usb_host_device_open(class_driver_obj.client_hdl, class_driver_obj.dev_addr, &class_driver_obj.dev_hdl));
				usb_host_interface_claim(class_driver_obj.client_hdl, class_driver_obj.dev_hdl, 0, 0);

				pvclass_driver_obj = &class_driver_obj;
				xQueueSend( xQueue, ( void * ) &pvclass_driver_obj, ( TickType_t ) 1 );
				ESP_LOGI(CLASS_TAG, "class_driver_obj.actions : %lx", class_driver_obj.actions);
				class_driver_obj.actions = 0x80;

//				escCallback("device connected", 1);
			}

			if (class_driver_obj.actions & CLASS_DRIVER_ACTION_CLOSE_DEV) {
				ESP_LOGI(CLASS_TAG, "class_driver_obj.actions : %lx", class_driver_obj.actions);
				ESP_LOGI(CLASS_TAG, "Close dev Client");
				//Release the interface and close the device
				usb_host_interface_release(class_driver_obj.client_hdl, class_driver_obj.dev_hdl, 0);
				usb_host_device_close(class_driver_obj.client_hdl, class_driver_obj.dev_hdl);
				xQueueReceive( xQueue, &(pvclass_driver_obj), ( TickType_t ) 3);
				exit = true;
			}
			//Handle any other actions required by the class driver
		}
		//Cleanup class driver
		ESP_LOGI(CLASS_TAG, "Deregistering Client");
	}
	//Wait to be deleted
    ESP_LOGI(CLASS_TAG, "xSemaphoreGive");
    xSemaphoreGive(signaling_sem);
    vTaskSuspend(NULL);
}

void print()
{
	usb_host_lib_info_t info_ret;

	struct class_driver_control *pvclass_driver_obj_print;

	if(xQueue != 0) {
		if(uxQueueMessagesWaiting(xQueue))
		{
			ESP_LOGI("xQueue", "messages waiting %d", uxQueueMessagesWaiting(xQueue));
			if( xQueuePeek( xQueue, &(pvclass_driver_obj_print), ( TickType_t ) 10) == pdTRUE ) {
				printf("client handled!\n");
				ESP_LOGI("print", "setting transfer");

				transfer->device_handle = pvclass_driver_obj_print->dev_hdl;
				transfer->bEndpointAddress = endPointAddressOut;
				transfer->callback = transfer_cb;
				transfer->context = (void *)&pvclass_driver_obj_print;
//				transfer->num_bytes = (((transfer_index)/64) + 1)*64;
				transfer->num_bytes = transfer_index;
//				vTaskDelay(50);
				esp_err_t err = usb_host_transfer_submit(transfer);
				if (err != ESP_OK) {
					ESP_LOGE("getPrinterStat", "OUT usb_host_transfer_submit In fail: %x", err);
					usb_host_transfer_free(transfer);
					return;
				}

				if(xSemaphoreTake(transfer_cb_sem, 200) == pdFALSE)
					printf("could not take semaphore\n");

				usb_host_transfer_free(transfer);
				transfer_index = 0;
			}
		}
	}
}

void getStat(uint8_t address, uint8_t status_code )
{

	usb_host_lib_info_t info_ret;
	usb_transfer_t *transfer_st;
	struct class_driver_control *pvclass_driver_obj_stat;

	uint8_t command[5] = {0x1B, 0x40, 0x10, 0x04, status_code};

//	usb_host_transfer_alloc(64, 0, &transfer_st);
	if(xQueue != 0) {
		if(uxQueueMessagesWaiting(xQueue))
		{
			if( xQueuePeek( xQueue, &(pvclass_driver_obj_stat), ( TickType_t ) 10) == pdTRUE )
			{
				usb_host_transfer_alloc(64, 0, &transfer_st);
				transfer_st->device_handle = pvclass_driver_obj_stat->dev_hdl;
				transfer_st->callback = getStatus_cb;
				transfer_st->bEndpointAddress = address;
				transfer_st->context = NULL;
				memset(transfer_st->data_buffer, 0x00, 64);
				if(address == endPointAddressOut)
					memcpy(transfer_st->data_buffer,command, 5);
				transfer_st->num_bytes = 64;
				esp_err_t err = usb_host_transfer_submit(transfer_st);
				if (err != ESP_OK) {
					ESP_LOGE("getPrinterStat", "OUT usb_host_transfer_submit In fail: %x", err);
					usb_host_transfer_free(transfer_st);
					return;
				}
			}
		}
	}
}
uint8_t getPrinterStat(uint8_t status_code) // TODO: DONE
{

	getStat(endPointAddressOut, status_code);
	if(xSemaphoreTake(callback_sem, 100) != pdTRUE){
		statusCode = 0;
		ESP_LOGE("getPrinterStat", "OUT without CB %d", endPointAddressOut);
		return 0;
	}

	getStat(endPointAddressIn, status_code);
	if(xSemaphoreTake(callback_sem, 100) != pdTRUE){
		statusCode = 0;
		ESP_LOGE("getPrinterStat", "IN without CB %d", endPointAddressOut);
		return 0;
	}
	return statusCode;
}
void getPrinterStatTask(void *arg){
	while(1){
//		ESP_LOGI("get_status", "start");
		if(uxQueueMessagesWaiting(xQueue)){
			xSemaphoreTake(printingQueue_sem, portMAX_DELAY);
			xSemaphoreTake(callback_sem, 500);
			uint8_t printerStatusCode = 0;
			uint8_t printerOfflineStatus = 0;
			uint8_t printerErrorStatus = 0;
			printerStatusCode = getPrinterStat(1);
			printerOfflineStatus = getPrinterStat(2);
			printerErrorStatus = getPrinterStat(3);

			if(printerStatusCode == 22){
				printerStatus = ONLINE;
			}
			else if(printerStatusCode != 22){
				printerStatus = DEGRADED;

				if(printerOfflineStatus == 54){
					printerStatusString = platenIsOpen;
				}
				else if(printerOfflineStatus == 50){
					printerStatusString = noPaper;
				}
				if (printerOfflineStatus == 26){
					printerStatusString = feedButtonPressed;
				}
				if((printerErrorStatus & 0x04) == 0x04)
					printerStatusString = mechanicalError;
				if ((printerErrorStatus & 0x08) == 0x08)
					printerStatusString = autoCutterError;
				if ((printerErrorStatus & 0x20) == 0x20)
					printerStatusString = unrecoverableError;
				if ((printerErrorStatus & 0x40) == 0x40)
					printerStatusString = autoRecoverableError;
			}
			ESP_LOGI("getPrinterStatTask", "1 : %d, 2 : %d, 3 : %d", printerStatusCode, printerOfflineStatus, printerErrorStatus);
			xSemaphoreGive(callback_sem);
			xSemaphoreGive(printingQueue_sem);
		}
		vTaskDelay(200);
	}
	vTaskSuspend(NULL);
}
void writeByte(uint8_t a){
	transfer->data_buffer[transfer_index] = (unsigned char)a;
	transfer_index ++;
}

static void writeByte2(uint8_t a,uint8_t b){
	writeByte(a);
	writeByte(b);
}

static void writeByte3(uint8_t a,uint8_t b, uint8_t c){
	writeByte(a);
	writeByte(b);
	writeByte(c);
}

static void writeBytes(uint8_t a,uint8_t b, uint8_t c, uint8_t d){
	writeByte(a);
	writeByte(b);
	writeByte(c);
	writeByte(d);
}

static void reset() {
  writeByte2(ASCII_ESC, '@'); // Init command
  prevByte = '\n';            // Treat as if prior line is blank
  column = 0;
  maxColumn = 32;
  charHeight = 24;
  lineSpacing = 6;
  barcodeHeight = 50;
}
static void flush()
{
	writeByte(ASCII_FF);
}
static void justify(char value) {
  uint8_t pos = 0;

  switch (toupper(value)) {
  case 'L':
    pos = 0;
    break;
  case 'C':
    pos = 1;
    break;
  case 'R':
    pos = 2;
    break;
  }

  writeByte3(ASCII_ESC, 'a', pos);
}

static void printBitmap(int w, int h, unsigned char* fromStream) {

    int h_h = h >> 8;
    w /= 8;

    writeBytes(ASCII_GS, 'v', '0', 0);
    writeBytes(w, 0, h - h_h*256 , h_h);
    int  x = w*h;


    for(int i = 0; i < x; i++){
    	writeByte(*(fromStream+i));
    }
}
static void writePrintMode() {
  writeByte3(ASCII_ESC, '!', printMode);
}

void adjustCharValues(uint8_t printMode) {
  uint8_t charWidth;
  if (printMode & FONT_MASK) {
    // FontB
    charHeight = 17;
    charWidth = 9;
  } else {
    // FontA
    charHeight = 24;
    charWidth = 12;
  }
  // Double Width Mode
  if (printMode & DOUBLE_WIDTH_MASK) {
    maxColumn /= 2;
    charWidth *= 2;
  }
  // Double Height Mode
  if (printMode & DOUBLE_HEIGHT_MASK) {
    charHeight *= 2;
  }
  maxColumn = (384 / charWidth);
}

static void setPrintMode(uint8_t mask) {
  printMode |= mask;
  writePrintMode();
  adjustCharValues(printMode);
  // charHeight = (printMode & DOUBLE_HEIGHT_MASK) ? 48 : 24;
  // maxColumn = (printMode & DOUBLE_WIDTH_MASK) ? 16 : 32;
}

void unsetPrintMode(uint8_t mask) {
  printMode &= ~mask;
  writePrintMode();
  adjustCharValues(printMode);
  // charHeight = (printMode & DOUBLE_HEIGHT_MASK) ? 48 : 24;
  // maxColumn = (printMode & DOUBLE_WIDTH_MASK) ? 16 : 32;
}

void doubleHeightOn() { setPrintMode(DOUBLE_HEIGHT_MASK); }

void doubleHeightOff() { unsetPrintMode(DOUBLE_HEIGHT_MASK); }

void doubleWidthOn() { setPrintMode(DOUBLE_WIDTH_MASK); }

void doubleWidthOff() { unsetPrintMode(DOUBLE_WIDTH_MASK); }

void boldOn() { setPrintMode(BOLD_MASK); }

void boldOff() { unsetPrintMode(BOLD_MASK); }

static void setFont(char font) {
  switch (toupper(font)) {
  case 'B':
    setPrintMode(FONT_MASK);
    break;
  case 'A':
  default:
    unsetPrintMode(FONT_MASK);
  }
}



void setSize(char value) {

  switch (toupper(value)) {
  default: // Small: standard width and height
    // size = 0x00;
    // charHeight = 24;
    // maxColumn = 32;
    doubleWidthOff();
    doubleHeightOff();
    break;
  case 'M': // Medium: double height
    // size = 0x01;
    // charHeight = 48;
    // maxColumn = 32;
    doubleHeightOn();
    doubleWidthOff();
    break;
  case 'L': // Large: double width and height
    // size = 0x11;
    // charHeight = 48;
    // maxColumn = 16;
    doubleHeightOn();
    doubleWidthOn();
    break;
  }

  // writeBytes(ASCII_GS, '!', size);
  // prevByte = '\n'; // Setting the size adds a linefeed
}
// Underlines of different weights can be produced:
// 0 - no underline
// 1 - normal underline
// 2 - thick underline
void underlineOn(uint8_t weight) {
  if (weight > 2)
    weight = 2;
  writeByte3(ASCII_ESC, '-', weight);
}

void underlineOff() { writeByte3(ASCII_ESC, '-', 0); }

void setLineHeight(int val) {
  // if (val < 24)
  //   val = 24;
  // lineSpacing = val - 24;

  // The printer doesn't take into account the current text height
  // when setting line height, making this more akin to inter-line
  // spacing.  Default line spacing is 30 (char height of 24, line
  // spacing of 6).
  writeByte3(ASCII_ESC, '3', val);
}
void partial_cut() {
  writeByte2(ASCII_ESC, 'm');
  //timeoutSet(rows * dotFeedTime);
  prevByte = '\n';
  column = 0;
}
void feed(uint8_t x) {
//  if (firmware >= 264) {
//    writeByte3(ASCII_ESC, 'd', x);
//    prevByte = '\n';
//    column = 0;
//  } else {
    while (x--)
      writeByte('\n'); // Feed manually; old firmware feeds excess lines

}
void print_text(const char* data, const char* size, bool bold, bool underline, const char* alignment, int line_spacing , char font )
{
  setFont(font);
  setSize(size[0]);
  if(bold)
    boldOn();
  else
    boldOff();

  if (underline) {
    underlineOn(1);
  }
  else {
    underlineOff();
  }

  justify(alignment[0]);
  setLineHeight(line_spacing);

	for(int i = 0; i < strlen(data); i++){
		writeByte(*(data+i));
	};
	writeByte('\n');
//  feed(10);

}

void print_image(int width, int height, const char* alignment, unsigned  char* image)
{
//  reset();
//  flush();
  justify(alignment[0]);
  printBitmap(width, height, image);
  setLineHeight(30);
  feed(1);
//  flush();
}
void printBarcode(const char *text, uint8_t type) {
  feed(1); // Recent firmware can't print barcode w/o feed first???
//  if (firmware >= 264)
//    type += 65;
  writeByte3(ASCII_GS, 'H', 2);    // Print label below barcode
  writeByte3(ASCII_GS, 'w', 3);    // Barcode width 3 (0.375/1.0mm thin/thick)
  writeByte3(ASCII_GS, 'k', type); // Barcode type (listed in .h file)
//  if (firmware >= 264) {
//    int len = strlen(text);
//    if (len > 255)
//      len = 255;
//    writeBytes(len); // Write length byte
//    for (uint8_t i = 0; i < len; i++)
//      writeBytes(text[i]); // Write string sans NUL
//  } else {
    uint8_t c, i = 0;
    do { // Copy string + NUL terminator
    	c = text[i++];
    	writeByte(c);
    } while (c);

//  timeoutSet((barcodeHeight + 40) * dotPrintTime);
  prevByte = '\n';
}

void setBarcodeHeight(uint8_t val) { // Default is 50
  if (val < 1)
    val = 1;
  barcodeHeight = val;
  writeByte3(ASCII_GS, 'h', val);
}


