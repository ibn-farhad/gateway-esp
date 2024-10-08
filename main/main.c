
/*
 * main.c
 *
 *  Created on: Aug 8, 2023
 *      Author: markscheider
 */

#include "barcode.h"
#include "ESC.h"
#include "trigger.h"
#include "relay.h"
#include "usb_camera.h"


#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "keep_alive.h"
#include "protocol_examples_common.h"
#include "esp_eth_enc28j60.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <string.h>
#include <stdbool.h>
#include <netdb.h>
#include "nvs_flash.h"
#include "driver/gpio.h"

#define WS_PORT 8080
#define MAX_CLIENT_NUM 6
#ifdef CONFIG_BARCODE_SCANNER
#define BARCODE
#endif
/* The examples use configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_WIFI_SSID             CONFIG_EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_PASS             CONFIG_EXAMPLE_WIFI_PASSWORD
#define EXAMPLE_MAXIMUM_RETRY         CONFIG_EXAMPLE_MAXIMUM_RETRY
#define EXAMPLE_STATIC_IP_ADDR        CONFIG_EXAMPLE_STATIC_IP_ADDR
#define EXAMPLE_STATIC_NETMASK_ADDR   CONFIG_EXAMPLE_STATIC_NETMASK_ADDR
#define EXAMPLE_STATIC_GW_ADDR        CONFIG_EXAMPLE_STATIC_GW_ADDR
#ifdef CONFIG_EXAMPLE_STATIC_DNS_AUTO
#define EXAMPLE_MAIN_DNS_SERVER       EXAMPLE_STATIC_GW_ADDR
#define EXAMPLE_BACKUP_DNS_SERVER     "0.0.0.0"
#else
#define EXAMPLE_MAIN_DNS_SERVER       CONFIG_EXAMPLE_STATIC_DNS_SERVER_MAIN
#define EXAMPLE_BACKUP_DNS_SERVER     CONFIG_EXAMPLE_STATIC_DNS_SERVER_BACKUP
#endif
#ifdef CONFIG_EXAMPLE_STATIC_DNS_RESOLVE_TEST
#define EXAMPLE_RESOLVE_DOMAIN        CONFIG_EXAMPLE_STATIC_RESOLVE_DOMAIN
#endif

#ifdef CONFIG_RELAY_INVERSE_CONTROL
#define RELAY_INVERSE_CONTROL_MAIN CONFIG_RELAY_INVERSE_CONTROL
#else
#define RELAY_INVERSE_CONTROL_MAIN 0
#endif
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *static_ip = "static_ip";

static int s_retry_num = 0;


static const char *TAG = "ws_echo_server";

//uint8_t buf[20000] = {0};
/*
 * Structure holding server handle
 * and internal socket fd in order
 * to use out of request send
 */

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
}resp_arg_defaults = {NULL, -1};

typedef struct async_resp_arg connected_client;

connected_client camera_clients[MAX_CLIENT_NUM];
connected_client printer_clients[MAX_CLIENT_NUM];
connected_client barcodeScanner_clients[MAX_CLIENT_NUM];
connected_client trigger_clients[MAX_CLIENT_NUM];
connected_client relay_clients[MAX_CLIENT_NUM];
connected_client LED_clients[MAX_CLIENT_NUM];
enum Devices{
	PRINTER,
	BARCODESCANNER,
	LEDDISPLAY,
	TRIGGER,
	RELAY
};
typedef enum Devices devices_t;

void softWD(void *arg);
void restartDaily(void *arg);
void heap_size_task(void *arg);

esp_err_t wss_open_fd(httpd_handle_t hd, int sockfd);

void wss_close_fd(httpd_handle_t hd, int sockfd);

bool check_client_alive_cb(wss_keep_alive_t h, int fd);

bool client_not_alive_cb(wss_keep_alive_t h, int fd);

static void send_ping(void *arg);

void device_cb(char *mes, httpd_handle_t handle, int device);

static void ws_async_send(void *arg);

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req);

static esp_err_t ws_handler(httpd_req_t *req);

static httpd_handle_t start_webserver(void);

static esp_err_t stop_webserver(httpd_handle_t server);

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data);
void print_client_ip(httpd_req_t *req);
esp_err_t camera_client(httpd_req_t *req);

//static const httpd_uri_t camera = {
//        .uri        = "/0",
//        .method     = HTTP_GET,
//        .handler    = camera_client,
//        .user_ctx   = NULL,
//        .is_websocket = true
//};

static const httpd_uri_t ws_printer = {
        .uri        = "/1",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
		.handle_ws_control_frames = true
};
static const httpd_uri_t ws_barcodeScanner = {
        .uri        = "/2",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
		.handle_ws_control_frames = true
};
static const httpd_uri_t ws_LED = {
        .uri        = "/3",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
		.handle_ws_control_frames = true
};
static const httpd_uri_t ws_trigger = {
        .uri        = "/4",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
		.handle_ws_control_frames = true
};
static const httpd_uri_t ws_relay = {
        .uri        = "/5",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
		.handle_ws_control_frames = true
};

void fillArray(connected_client* array, connected_client  defaultValue);
bool valueinarray(int val, connected_client *arr, size_t len);

void wifi_init_sta(void);
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
static void example_set_static_ip(esp_netif_t *netif);
static esp_err_t example_set_dns_server(esp_netif_t *netif, uint32_t addr, esp_netif_dns_type_t type);



void app_main(void)
{
    gpio_config_t ethernet_reset_config = {
			.intr_type = GPIO_INTR_DISABLE,
			.mode = GPIO_MODE_OUTPUT,
			.pin_bit_mask = (1ULL<<46),
			.pull_down_en = GPIO_PULLDOWN_ENABLE,
			.pull_up_en = GPIO_PULLUP_DISABLE
	};

	gpio_config(&ethernet_reset_config);
	gpio_set_level(46, 1);
	xTaskCreate(softWD, "softWatchdog", 4096, NULL, 16, NULL);

#ifdef BARCODE
	initBarcodeScanner(device_cb);
#endif

#ifdef CONFIG_ESC_PRINTER
	initESC(device_cb);
#endif

#ifdef CONFIG_TRIGGER
	initTrigger(device_cb);
#endif

#ifdef CONFIG_RELAY
	initRelay(device_cb);
#endif

#ifdef CONFIG_LED_MATRIX
	initLEDDevice(device_cb);
#endif

	fillArray(camera_clients, resp_arg_defaults);
	fillArray(printer_clients, resp_arg_defaults);
	fillArray(barcodeScanner_clients, resp_arg_defaults);
	fillArray(trigger_clients, resp_arg_defaults);
	fillArray(relay_clients, resp_arg_defaults);
	fillArray(LED_clients, resp_arg_defaults);
    httpd_handle_t server = NULL;


//	if (RELAY_INVERSE_CONTROL == 1)
//
//	else
//		gpio_set_level(RELAY_PIN, 0);

//    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
//     * Read "Establishing Wi-Fi or Ethernet Connection" section in
//     * examples/protocols/README.md for more information about this function.
//     */
//    ESP_ERROR_CHECK(example_connect());
//
//    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
//     * and re-start it upon connection.
//     */
//#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
////    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
//    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID , &connect_handler, &server));
//    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
//#endif // CONFIG_EXAMPLE_CONNECT_WIFI

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(static_ip, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
#endif // CONFIG_EXAMPLE_CONNECT_WIFI

#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "*****************");
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(TAG, "*****************");

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

    /* Start the server for the first time */
    server = start_webserver();
//    xTaskCreate(heap_size_task, "heap_size_task", 4096, NULL, 1, NULL);
    xTaskCreate(restartDaily, "restart_daily", 2048, NULL, 1, NULL);

#ifdef CONFIG_CAMERA
	initCamera(device_cb, camera_clients, &server);

#endif

}

void restartDaily(void *arg)
{
	while(1){
		vTaskDelay(100*86400);
		example_disconnect();
		gpio_set_level(5,1);
		vTaskDelay(100);
		gpio_set_level(5,0);
		vTaskDelay(100);
		gpio_set_level(46,0);
		vTaskDelay(200);
		esp_restart();
	}
}

void heap_size_task(void *arg)
{
	while(1){
		vTaskDelay(400);
		ESP_LOGI(TAG, "free heap size %d", xPortGetFreeHeapSize());
	}
}
void softWD(void *arg){
	while(1)
	{
		vTaskDelay(100*30);
#ifdef CONFIG_RELAY
		if (RELAY_INVERSE_CONTROL_MAIN == 1)
			gpio_set_level(RELAY_PIN, 1);
		else
			gpio_set_level(RELAY_PIN, 0);
#endif
		for(int i = 0; i < MAX_CLIENT_NUM; i++)
		{
			if((printer_clients[i].fd != -1) || (barcodeScanner_clients[i].fd != -1) || (trigger_clients[i].fd != -1) || (relay_clients[i].fd != -1) || (LED_clients[i].fd != -1))
				break;

			if(i == (MAX_CLIENT_NUM - 1))
			{
				example_disconnect();
				gpio_set_level(5,1);
				vTaskDelay(100);
				gpio_set_level(5,0);
				vTaskDelay(100);
				gpio_set_level(46,0);
				vTaskDelay(200);
				esp_restart();
			}
		}
		ESP_LOGI("softWD", "at least one connected client");
	}
}

esp_err_t wss_open_fd(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "New client connected %d", sockfd);

    wss_keep_alive_t h = httpd_get_global_user_ctx(hd);
    return wss_keep_alive_add_client(h, sockfd);
}

void wss_close_fd(httpd_handle_t hd, int sockfd)
{
    ESP_LOGI(TAG, "Client disconnected %d", sockfd);
    wss_keep_alive_t h = httpd_get_global_user_ctx(hd);
    wss_keep_alive_remove_client(h, sockfd);
    for(int i = 0; i < MAX_CLIENT_NUM; i++)
	{
    	if(camera_clients[i].fd == sockfd){
			ESP_LOGI(TAG, "camera_clients[%d].fd : %d", i, camera_clients[i].fd);
			camera_clients[i] = resp_arg_defaults;
			ESP_LOGI(TAG, "camera_clients[%d].fd : %d", i, camera_clients[i].fd);
			break;
		}
		if(printer_clients[i].fd == sockfd){
			ESP_LOGI(TAG, "printer_clients[%d].fd : %d", i, printer_clients[i].fd);
			printer_clients[i] = resp_arg_defaults;
			ESP_LOGI(TAG, "printer_clients[%d].fd : %d", i, printer_clients[i].fd);
			break;
		}
		if(barcodeScanner_clients[i].fd == sockfd){
			ESP_LOGI(TAG, "barcodeScanner_clients[%d].fd : %d", i, barcodeScanner_clients[i].fd);
			barcodeScanner_clients[i] = resp_arg_defaults;
			ESP_LOGI(TAG, "barcodeScanner_clients[%d].fd : %d", i, barcodeScanner_clients[i].fd);
			break;
		}
		if(trigger_clients[i].fd == sockfd){
			ESP_LOGI(TAG, "trigger_clients[%d].fd : %d", i, trigger_clients[i].fd);
			trigger_clients[i] = resp_arg_defaults;
			ESP_LOGI(TAG, "trigger_clients[%d].fd : %d", i, trigger_clients[i].fd);
			break;
		}
		if(relay_clients[i].fd == sockfd){
			ESP_LOGI(TAG, "relay_clients[%d].fd : %d", i, relay_clients[i].fd);
			relay_clients[i] = resp_arg_defaults;
			ESP_LOGI(TAG, "relay_clients[%d].fd : %d", i, relay_clients[i].fd);
			break;
		}
		if(LED_clients[i].fd == sockfd){
			ESP_LOGI(TAG, "LED_clients[%d].fd : %d", i, LED_clients[i].fd);
			LED_clients[i] = resp_arg_defaults;
			ESP_LOGI(TAG, "LED_clients[%d].fd : %d", i, LED_clients[i].fd);
			break;
		}
		//TODO: Add another devises
	}
    close(sockfd);
}

bool client_not_alive_cb(wss_keep_alive_t h, int fd)
{
    ESP_LOGE(TAG, "Client not alive, closing fd %d", fd);
    esp_err_t ret = httpd_sess_trigger_close(wss_keep_alive_get_user_ctx(h), fd);
    ESP_LOGE(TAG, "httpd_sess_trigger_close returned %d", ret);
    return true;
}

bool check_client_alive_cb(wss_keep_alive_t h, int fd)
{
    ESP_LOGD(TAG, "Checking if client (fd=%d) is alive", fd);
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    resp_arg->hd = wss_keep_alive_get_user_ctx(h);
    resp_arg->fd = fd;

    if (httpd_queue_work(resp_arg->hd, send_ping, resp_arg) == ESP_OK) {
    	return true;
    }
    return false;
}

static void send_ping(void *arg)
{
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = NULL;
    ws_pkt.len = 0;
    ws_pkt.type = HTTPD_WS_TYPE_PING;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}

void device_cb(char *mes, httpd_handle_t handle, int device){
	httpd_ws_frame_t ws_pkt;
	memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
	ws_pkt.payload = (uint8_t*)mes;
	ws_pkt.len = strlen(mes);
	ws_pkt.type = HTTPD_WS_TYPE_TEXT;

	if(handle == NULL){
		for(int i = 0; i < MAX_CLIENT_NUM; i++){
			switch (device) {
				case 0:
				{
					if(printer_clients[i].hd != NULL)
						httpd_ws_send_frame_async(camera_clients[i].hd, camera_clients[i].fd, &ws_pkt);
					break;
				}
				case 1:
				{
					if(printer_clients[i].hd != NULL)
						httpd_ws_send_frame_async(printer_clients[i].hd, printer_clients[i].fd, &ws_pkt);
					break;
				}
				case 2:
				{
					if(barcodeScanner_clients[i].hd != NULL)
						httpd_ws_send_frame_async(barcodeScanner_clients[i].hd, barcodeScanner_clients[i].fd, &ws_pkt);
					break;
				}
				case 3:
				{
					if(LED_clients[i].hd != NULL)
						httpd_ws_send_frame_async(LED_clients[i].hd, LED_clients[i].fd, &ws_pkt);
					break;
				}
				case 4:
				{
					if(trigger_clients[i].hd != NULL)
						httpd_ws_send_frame_async(trigger_clients[i].hd, trigger_clients[i].fd, &ws_pkt);
					break;
				}
				case 5:
				{
					if(relay_clients[i].hd != NULL)
						httpd_ws_send_frame_async(relay_clients[i].hd, relay_clients[i].fd, &ws_pkt);
					break;
				}
				default:
					break;
			}
		}
	}
	else if (handle != NULL) {
		httpd_ws_send_frame_async(handle, device, &ws_pkt);
	}
	free(mes);
//	free(ws_pkt.payload);
	printf("JSON_print freed\n\r");

}
/*
 * async send function, which we put into the httpd work queue
 */
static void ws_async_send(void *arg)
{
    static const char * data = "Async data";
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)data;
    ws_pkt.len = strlen(data);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_arg);
}

void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    ESP_LOGI("connect_handler", "event_id : %ld\n", event_id);
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
//        *server = start_webserver();
    }
}

esp_err_t camera_client(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        print_client_ip(req);
        ESP_LOGI(TAG, "if req method : %d", req->method);
        for (int i = 0; i < MAX_CLIENT_NUM; ++i)
		{
			if (camera_clients[i].fd == -1) {
				camera_clients[i].hd = req->handle;
				camera_clients[i].fd = httpd_req_to_sockfd(req);
				ESP_LOGI(TAG, "printer_clients[%d].fd : %d", i, camera_clients[i].fd);
				break;
			}
		}
        return ESP_OK;

    }
    ESP_LOGI(TAG, "req method : %d", req->method);
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
//        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
//        printf("length buf : %d", ws_pkt.len);
    }
    ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);
    if(ws_pkt.type == HTTPD_WS_TYPE_CLOSE){
		for(int i = 0; i < MAX_CLIENT_NUM; i++)
		{
			if(camera_clients[i].fd == httpd_req_to_sockfd(req)){
				ESP_LOGI(TAG, "printer_clients[%d].fd : %d", i, camera_clients[i].fd);
				camera_clients[i] = resp_arg_defaults;
				ESP_LOGI(TAG, "printer_clients[%d].fd : %d", i, camera_clients[i].fd);
				break;
			}
		}
    }
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
        strcmp((char*)ws_pkt.payload,"Trigger async") == 0) {
        free(buf);
        return trigger_async_send(req->handle, req);
    }

    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
    free(buf);
    return ret;
}

/*
 * This handler echos back the received ws data
 * and triggers an async send if certain message received
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "req->method : %d", req->method);
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, client uri is : %s", req->uri);
        print_client_ip(req);
        int deviceNumber = -1;
        sscanf(req->uri,"/%d", &deviceNumber);
        switch (deviceNumber) {
			case 1:
			{
				for (int i = 0; i < MAX_CLIENT_NUM; ++i)
				{
					if (printer_clients[i].fd == -1) {
						printer_clients[i].hd = req->handle;
						printer_clients[i].fd = httpd_req_to_sockfd(req);
						ESP_LOGI(TAG, "printer_clients[%d].fd : %d", i, printer_clients[i].fd);
						break;
					}
				}
				break;
			}
			case 2:
			{
				for (int i = 0; i < MAX_CLIENT_NUM; ++i)
				{
					if (barcodeScanner_clients[i].fd == -1) {
						barcodeScanner_clients[i].hd = req->handle;
						barcodeScanner_clients[i].fd = httpd_req_to_sockfd(req);
						ESP_LOGI(TAG, "barcodeScanner_clients[%d].fd : %d", i, barcodeScanner_clients[i].fd);
						break;
					}
				}
				break;
			}
			case 3:
			{
				for (int i = 0; i < MAX_CLIENT_NUM; ++i)
				{
					if (LED_clients[i].fd == -1) {
						LED_clients[i].hd = req->handle;
						LED_clients[i].fd = httpd_req_to_sockfd(req);
						ESP_LOGI(TAG, "LED_clients[%d].fd : %d", i, LED_clients[i].fd);
						break;
					}
				}
				break;
			}

			case 4:
			{
				for (int i = 0; i < MAX_CLIENT_NUM; ++i)
				{
					if (trigger_clients[i].fd == -1) {
						trigger_clients[i].hd = req->handle;
						trigger_clients[i].fd = httpd_req_to_sockfd(req);
						ESP_LOGI(TAG, "trigger_clients[%d].fd : %d", i, trigger_clients[i].fd);
						break;
					}
				}
				break;
			}
			case 5:
			{
				for (int i = 0; i < MAX_CLIENT_NUM; ++i)
				{
					if (relay_clients[i].fd == -1) {
						relay_clients[i].hd = req->handle;
						relay_clients[i].fd = httpd_req_to_sockfd(req);
						ESP_LOGI(TAG, "relay_clients[%d].fd : %d", i, relay_clients[i].fd);
						break;
					}
				}
				break;
			}
			default:
				break;
		}
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
//    ESP_LOGI(TAG, "Packet type: %d", ws_pkt.type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
//        esp_restart();
        return ret;
    }
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
//    	ESP_LOGI(TAG, "free heap size %d", xPortGetFreeHeapSize());

        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
//        printf("length buf : %d", ws_pkt.len);
//        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    switch (ws_pkt.type) {
		case HTTPD_WS_TYPE_CLOSE:
		{
			for(int i = 0; i < MAX_CLIENT_NUM; i++)
			{
				if(printer_clients[i].fd == httpd_req_to_sockfd(req)){
					ESP_LOGI(TAG, "printer_clients[%d].fd : %d", i, printer_clients[i].fd);
					printer_clients[i] = resp_arg_defaults;
					ESP_LOGI(TAG, "printer_clients[%d].fd : %d", i, printer_clients[i].fd);
					break;
				}
				if(barcodeScanner_clients[i].fd == httpd_req_to_sockfd(req)){
					ESP_LOGI(TAG, "barcodeScanner_clients[%d].fd : %d", i, barcodeScanner_clients[i].fd);
					barcodeScanner_clients[i] = resp_arg_defaults;
					ESP_LOGI(TAG, "barcodeScanner_clients[%d].fd : %d", i, barcodeScanner_clients[i].fd);
					break;
				}
				if(LED_clients[i].fd == httpd_req_to_sockfd(req)){
					ESP_LOGI(TAG, "LED_clients[%d].fd : %d", i, LED_clients[i].fd);
					LED_clients[i] = resp_arg_defaults;
					ESP_LOGI(TAG, "LED_clients[%d].fd : %d", i, LED_clients[i].fd);
					break;
				}
				if(trigger_clients[i].fd == httpd_req_to_sockfd(req)){
					ESP_LOGI(TAG, "trigger_clients[%d].fd : %d", i, trigger_clients[i].fd);
					trigger_clients[i] = resp_arg_defaults;
					ESP_LOGI(TAG, "trigger_clients[%d].fd : %d", i, trigger_clients[i].fd);
					break;
				}
				if(relay_clients[i].fd == httpd_req_to_sockfd(req)){
					ESP_LOGI(TAG, "relay_clients[%d].fd : %d", i, relay_clients[i].fd);
					relay_clients[i] = resp_arg_defaults;
					ESP_LOGI(TAG, "relay_clients[%d].fd : %d", i, relay_clients[i].fd);
					break;
				}
			}

			ESP_LOGI(TAG, "client disconnected fd : %d", httpd_req_to_sockfd(req));
			break;
		}
		case HTTPD_WS_TYPE_TEXT:
		{
			ESP_LOGI(TAG, "HTTPD_WS_TYPE_TEXT");
#ifdef CONFIG_ESC_PRINTER
			if(valueinarray(httpd_req_to_sockfd(req), printer_clients, MAX_CLIENT_NUM)){
//				ESP_LOGI(TAG,"data->printer : \%s\"",(char*)ws_pkt.payload);

				escHandleCommand((char*)ws_pkt.payload, req->handle, httpd_req_to_sockfd(req));
			}
#endif
#ifdef BARCODE
			if(valueinarray(httpd_req_to_sockfd(req), barcodeScanner_clients, MAX_CLIENT_NUM)){
				barcodeHandleCommand((char*)ws_pkt.payload, req->handle, httpd_req_to_sockfd(req));

			}
#endif
#ifdef CONFIG_LED_MATRIX
			if(valueinarray(httpd_req_to_sockfd(req), LED_clients, MAX_CLIENT_NUM)){
				LEDHandleCommand((char*)ws_pkt.payload, req->handle, httpd_req_to_sockfd(req));

			}
#endif
#ifdef CONFIG_TRIGGER
			if(valueinarray(httpd_req_to_sockfd(req), trigger_clients, MAX_CLIENT_NUM)){
				triggerHandleCommand((char*)ws_pkt.payload, req->handle, httpd_req_to_sockfd(req));

			}
#endif
#ifdef CONFIG_RELAY
			if(valueinarray(httpd_req_to_sockfd(req), relay_clients, MAX_CLIENT_NUM)){
				relayHandleCommand((char*)ws_pkt.payload, req->handle, httpd_req_to_sockfd(req));
			}
#endif
			break;
		}

		case HTTPD_WS_TYPE_PING:
		{
//			callback((char*)ws_pkt.payload, req->handle, httpd_req_to_sockfd(req));
			httpd_ws_frame_t ws_pkt1 = {
					.final = ws_pkt.final,
					.fragmented = ws_pkt.fragmented,
					.len = 0,
					.payload = NULL,
					.type = HTTPD_WS_TYPE_PONG
			};
//			ws_pkt1.type = HTTPD_WS_TYPE_PONG;
			httpd_ws_send_frame(req, &ws_pkt1);
			break;
		}
		case HTTPD_WS_TYPE_PONG:
		{
			free(buf);
			return wss_keep_alive_client_is_active(httpd_get_global_user_ctx(req->handle),
					httpd_req_to_sockfd(req));
		}

		default:
			break;
	}
    free(buf);
    printf("buff cleaned\n\r");
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
	// Prepare keep-alive engine
	wss_keep_alive_config_t keep_alive_config = KEEP_ALIVE_CONFIG_DEFAULT();
	keep_alive_config.keep_alive_period_ms = 15000;
	keep_alive_config.not_alive_after_ms = 20000;
	keep_alive_config.task_stack_size = 8196;
	keep_alive_config.max_clients = MAX_CLIENT_NUM;
	keep_alive_config.client_not_alive_cb = client_not_alive_cb;
	keep_alive_config.check_client_alive_cb = check_client_alive_cb;
	wss_keep_alive_t keep_alive = wss_keep_alive_start(&keep_alive_config);

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = WS_PORT;
    config.max_open_sockets = MAX_CLIENT_NUM;
    config.global_user_ctx = keep_alive;
    config.open_fn = wss_open_fd;
    config.close_fn = wss_close_fd;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Registering the ws handler
        ESP_LOGI(TAG, "Registering URI handlers");
        /*
         * printer WS Handler
         */
        httpd_register_uri_handler(server, &ws_printer);
        httpd_register_uri_handler(server, &ws_barcodeScanner);
        httpd_register_uri_handler(server, &ws_trigger);
        httpd_register_uri_handler(server, &ws_relay);
        httpd_register_uri_handler(server, &ws_LED);
//        httpd_register_uri_handler(server, &camera);

        wss_keep_alive_set_user_ctx(keep_alive, server);
        /*
         * other devices
         */
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        example_disconnect();
		vTaskDelay(80);
		example_connect();
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

//static void connect_handler(void* arg, esp_event_base_t event_base,
//                            int32_t event_id, void* event_data)
//{
//    httpd_handle_t* server = (httpd_handle_t*) arg;
//    if (*server == NULL) {
//        ESP_LOGI(TAG, "Starting webserver");
////        *server = start_webserver();
//    }
//}

/*
 * This function prints client IP that sent http request
 */
void print_client_ip(httpd_req_t *req)
{
    int sockfd = httpd_req_to_sockfd(req);
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
    socklen_t addr_size = sizeof(addr);

    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_size) < 0) {
        ESP_LOGE(TAG, "Error getting client IP");
        return;
    }

    // Convert to IPv6 string
    inet_ntop(AF_INET, &addr.sin6_addr.un.u32_addr[3], ipstr, sizeof(ipstr));
	ESP_LOGI(TAG, "Client IP => %s", ipstr);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    return httpd_queue_work(handle, ws_async_send, resp_arg);
}

void fillArray(connected_client* array, connected_client  defaultValue)
{
	for(int i = 0; i < MAX_CLIENT_NUM; ++i){
		array[i] = defaultValue;
		ESP_LOGI(TAG, "size : %d, i : %d, fd : %d", 10, i, array[i].fd);
	}

}

bool valueinarray(int val, connected_client *arr, size_t len) {
    for(size_t i = 0; i < len; i++) {
        if(arr[i].fd == val)
            return true;
    }
    return false;
}

static esp_err_t example_set_dns_server(esp_netif_t *netif, uint32_t addr, esp_netif_dns_type_t type)
{
    if (addr && (addr != IPADDR_NONE)) {
        esp_netif_dns_info_t dns;
        dns.ip.u_addr.ip4.addr = addr;
        dns.ip.type = IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, type, &dns));
    }
    return ESP_OK;
}

static void example_set_static_ip(esp_netif_t *netif)
{
    if (esp_netif_dhcpc_stop(netif) != ESP_OK) {
        ESP_LOGE(static_ip, "Failed to stop dhcp client");
        return;
    }
    esp_netif_ip_info_t ip;
    memset(&ip, 0 , sizeof(esp_netif_ip_info_t));
    ip.ip.addr = ipaddr_addr(EXAMPLE_STATIC_IP_ADDR);
    ip.netmask.addr = ipaddr_addr(EXAMPLE_STATIC_NETMASK_ADDR);
    ip.gw.addr = ipaddr_addr(EXAMPLE_STATIC_GW_ADDR);
    if (esp_netif_set_ip_info(netif, &ip) != ESP_OK) {
        ESP_LOGE(static_ip, "Failed to set ip info");
        return;
    }
    ESP_LOGD(static_ip, "Success to set static ip: %s, netmask: %s, gw: %s", EXAMPLE_STATIC_IP_ADDR, EXAMPLE_STATIC_NETMASK_ADDR, EXAMPLE_STATIC_GW_ADDR);
    ESP_ERROR_CHECK(example_set_dns_server(netif, ipaddr_addr(EXAMPLE_MAIN_DNS_SERVER), ESP_NETIF_DNS_MAIN));
    ESP_ERROR_CHECK(example_set_dns_server(netif, ipaddr_addr(EXAMPLE_BACKUP_DNS_SERVER), ESP_NETIF_DNS_BACKUP));
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        example_set_static_ip(arg);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(static_ip, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(static_ip,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(static_ip, "static ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        sta_netif,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        sta_netif,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(static_ip, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(static_ip, "connected to ap SSID:%s password:%s",
                 EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(static_ip, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASS);
    } else {
        ESP_LOGE(static_ip, "UNEXPECTED EVENT");
    }
#ifdef CONFIG_EXAMPLE_STATIC_DNS_RESOLVE_TEST
    struct addrinfo *address_info;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int res = getaddrinfo(EXAMPLE_RESOLVE_DOMAIN, NULL, &hints, &address_info);
    if (res != 0 || address_info == NULL) {
        ESP_LOGE(static_ip, "couldn't get hostname for :%s: "
                      "getaddrinfo() returns %d, addrinfo=%p", EXAMPLE_RESOLVE_DOMAIN, res, address_info);
    } else {
        if (address_info->ai_family == AF_INET) {
            struct sockaddr_in *p = (struct sockaddr_in *)address_info->ai_addr;
            ESP_LOGI(static_ip, "Resolved IPv4 address: %s", ipaddr_ntoa((const ip_addr_t*)&p->sin_addr.s_addr));
        }
#if CONFIG_LWIP_IPV6
        else if (address_info->ai_family == AF_INET6) {
            struct sockaddr_in6 *p = (struct sockaddr_in6 *)address_info->ai_addr;
            ESP_LOGI(static_ip, "Resolved IPv6 address: %s", ip6addr_ntoa((const ip6_addr_t*)&p->sin6_addr));
        }
#endif
    }
#endif
    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

