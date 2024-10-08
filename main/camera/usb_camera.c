/*
 * usb_camera.c
 *
 *  Created on: Jan 11, 2024
 *      Author: markscheider
 */


#include "usb_camera.h"

#ifdef CONFIG_CAMERA

#include <stdio.h>
#include <unistd.h>
#include "esp_log.h"
#include "libuvc/libuvc.h"
#include "libuvc_helper.h"
#include "libuvc_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/ringbuf.h"
#include "driver/gpio.h"
#include "usb/usb_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "USB_CAMERA";

#define USB_DISCONNECT_PIN  GPIO_NUM_0

#define FPS 10
#define WIDTH 160
#define HEIGHT 120
#define FORMAT UVC_FRAME_FORMAT_MJPEG // UVC_COLOR_FORMAT_YUYV

// Attached camera can be filtered out based on (non-zero value of) PID, VID, SERIAL_NUMBER
#define PID 0
#define VID 0
#define SERIAL_NUMBER NULL

#define UVC_CHECK(exp) do {                 \
    uvc_error_t _err_ = (exp);              \
    if(_err_ < 0) {                         \
        ESP_LOGE(TAG, "UVC error: %s",      \
                 uvc_error_string(_err_));  \
        assert(0);                          \
    }                                       \
} while(0)

static SemaphoreHandle_t ready_to_uninstall_usb;
static EventGroupHandle_t app_flags;

RingbufHandle_t  frame_buffer;

callb camera_cb;

struct async_resp_args {
    httpd_handle_t hd;
    int fd;
}resp_arg_defs = {NULL, -1};

typedef struct async_resp_args connected_clnt;

connected_clnt *clients;

static uvc_context_t *ctx1;
uvc_device_t *dev;
uvc_device_handle_t *devh;
uvc_stream_ctrl_t ctrl;
uvc_error_t res;

static void libuvc_adapter_cb(libuvc_adapter_event_t event)
{
    xEventGroupSetBits(app_flags, event);
}

static void usb_lib_handler_task(void *args)
{
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        // Release devices once all clients has deregistered
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        // Give ready_to_uninstall_usb semaphore to indicate that USB Host library
        // can be deinitialized, and terminate this task.
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            xSemaphoreGive(ready_to_uninstall_usb);
        }
    }

    vTaskDelete(NULL);
}

void button_callback(int button, int state, void *user_ptr)
{
    printf("button %d state %d\n", button, state);
}

esp_err_t initialize_usb_host_lib(void)
{
    TaskHandle_t task_handle = NULL;

    const usb_host_config_t host_config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1
    };

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
    	printf("%d\n\r", err);
        return err;
    }

    ready_to_uninstall_usb = xSemaphoreCreateBinary();
    if (ready_to_uninstall_usb == NULL) {
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(usb_lib_handler_task, "usb_events", 4096, NULL, 2, &task_handle) != pdPASS) {
        vSemaphoreDelete(ready_to_uninstall_usb);
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t ws_send(uint8_t *payload, size_t size)
{
	for(int i = 0; i < 3; i++)
	{
		if(clients[i].fd != -1)
		{
			if ( xRingbufferSend(frame_buffer, payload, size, pdMS_TO_TICKS(1)) != pdTRUE ) {
				ESP_LOGW(TAG, "Failed to send frame to ring buffer.");
				return ESP_FAIL;
			}
			break;
		}
	}
    return ESP_OK;
}

void frame_callback(uvc_frame_t *frame, void *ptr)
{
    static size_t fps;
    static size_t bytes_per_second;
    static int64_t start_time;

    int64_t current_time = esp_timer_get_time();
    bytes_per_second += frame->data_bytes;
    fps++;

    if (!start_time) {
        start_time = current_time;
    }

    if (current_time > start_time + 1000000) {
        ESP_LOGI(TAG, "fps: %u, bytes per second: %u", fps, bytes_per_second);
        start_time = current_time;
        bytes_per_second = 0;
        fps = 0;
    }

    // Stream received frame to client, if enabled
    ws_send(frame->data, frame->data_bytes);
}

static void sender_task(void *arg)
{

    while (1) {

        size_t bytes_received = 0;
        char *payload = (char *)xRingbufferReceiveUpTo(
            frame_buffer, &bytes_received, pdMS_TO_TICKS(2500), 20000);

        if (payload != NULL ) {
        	httpd_ws_frame_t ws_pkt;
			memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
			ws_pkt.payload = (uint8_t*)payload;
			ws_pkt.len = bytes_received;
			ws_pkt.type = HTTPD_WS_TYPE_BINARY;
			esp_err_t send_status = ESP_OK;


        	for(int i = 0; i < 3; i++)
        	{
        		if(clients[i].fd != -1)
        		{
//        			printf("camera_clients[i].fd = %d\n\r", camera_clients[i].fd);
        			send_status = httpd_ws_send_data(clients[i].hd, clients[i].fd, &ws_pkt);

        			if(send_status != ESP_OK)
        				ESP_ERROR_CHECK_WITHOUT_ABORT(send_status);
        		}
        	}
            vRingbufferReturnItem(frame_buffer, (void *)payload);
        }
    }
}

static EventBits_t wait_for_event(EventBits_t event)
{
    return xEventGroupWaitBits(app_flags, event, pdTRUE, pdFALSE, portMAX_DELAY) & event;
}

static void main_loop(void *arg){


	 do {

		printf("Waiting for device\n");
		wait_for_event(UVC_DEVICE_CONNECTED);

		UVC_CHECK( uvc_find_device(ctx1, &dev, PID, VID, SERIAL_NUMBER) );
		puts("Device found");

		UVC_CHECK( uvc_open(dev, &devh) );

		// Uncomment to print configuration descriptor
		 libuvc_adapter_print_descriptors(devh);

		uvc_set_button_callback(devh, button_callback, NULL);

		// Print known device information
		uvc_print_diag(devh, stderr);

		// Negotiate stream profile
		res = uvc_get_stream_ctrl_format_size(devh, &ctrl, FORMAT, WIDTH, HEIGHT, FPS );
		while (res != UVC_SUCCESS) {
			printf("Negotiating streaming format failed, trying again... %d\n", res);
			res = uvc_get_stream_ctrl_format_size(devh, &ctrl, FORMAT, WIDTH, HEIGHT, FPS );
			sleep(1);
		}

		ctrl.dwMaxPayloadTransferSize = 512;

		uvc_print_stream_ctrl(&ctrl, stderr);

		UVC_CHECK( uvc_start_streaming(devh, &ctrl, frame_callback, NULL, 0) );
		puts("Streaming...");

		wait_for_event(UVC_DEVICE_DISCONNECTED);

		uvc_stop_streaming(devh);
		puts("Done streaming.");

		uvc_close(devh);

	} while (1);
}

void initCamera(callb callback, void* camera_clnts, httpd_handle_t* server){

	clients = (connected_clnt*)camera_clnts;

//	httpd_handle_t ws_server = NULL;
	TaskHandle_t task_handle = NULL;
//	ws_server = *server;


	app_flags = xEventGroupCreate();
	assert(app_flags);

	const gpio_config_t input_pin = {
		.pin_bit_mask = BIT64(USB_DISCONNECT_PIN),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
	};
	ESP_ERROR_CHECK( gpio_config(&input_pin) );

	ESP_ERROR_CHECK( initialize_usb_host_lib() );
//	ESP_LOGI(TAG, "initialize_usb_host_lib - %d", initialize_usb_host_lib());

	libuvc_adapter_config_t config = {
		.create_background_task = true,
		.task_priority = 5,
		.stack_size = 8192,
		.callback = libuvc_adapter_cb
	};

	libuvc_adapter_set_config(&config);

	UVC_CHECK( uvc_init(&ctx1, NULL) );

	// Streaming takes place only when enabled in menuconfig
//    ESP_ERROR_CHECK_WITHOUT_ABORT( ws_wait_for_connection());
//	ESP_LOGI(TAG, " wait for connection => %d", ws_wait_for_connection());

	frame_buffer = xRingbufferCreate(100000, RINGBUF_TYPE_BYTEBUF);
	if ( frame_buffer == NULL) {
		ESP_LOGE(TAG,"ESP_ERR_NO_MEM");
	}

	BaseType_t task_created = xTaskCreate(sender_task, "sender_task", 4096, *server, 10, &task_handle);
	if (!task_created) {
		ESP_LOGE(TAG,"ESP_ERR_NO_MEM");
	}

	task_created = xTaskCreate(main_loop, "main_loop", 8192, NULL, 10, NULL);
	if (!task_created) {
		ESP_LOGE(TAG,"ESP_ERR_NO_MEM");
	}
}
#endif /* CONFIG_CAMERA */
