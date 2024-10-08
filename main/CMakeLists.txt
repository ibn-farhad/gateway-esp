idf_component_register(
					SRCS "main.c"
					SRCS "keep_alive.c"
					SRCS "printer/esc.c"
					SRCS "barcode_scanner/barcode.c"
					SRCS "relay/relay.c"
					SRCS "trigger/trigger.c"
					SRCS "led_matrix/led_device.c"
					SRCS "camera/usb_camera.c"
					
					INCLUDE_DIRS ""
					INCLUDE_DIRS "."	
                    INCLUDE_DIRS "printer"
                    INCLUDE_DIRS "barcode_scanner"
                    INCLUDE_DIRS "trigger"
                    INCLUDE_DIRS "relay"
                    INCLUDE_DIRS "led_matrix"
                    INCLUDE_DIRS "camera"
                    
#                    REQUIRES espressif__usb_host_uvc mbedtls json protocol_examples_common nvs_flash usb esp_ringbuf esp_timer esp_wifi driver esp_http_server esp_eth esp_netif lwip)
)