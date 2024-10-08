/*
 * usb_camera.h
 *
 *  Created on: Jan 11, 2024
 *      Author: markscheider
 */

#ifndef MAIN_CAMERA_USB_CAMERA_H_
#define MAIN_CAMERA_USB_CAMERA_H_

#include "sdkconfig.h"

#ifdef CONFIG_CAMERA
#include "esp_http_server.h"


typedef void (*callb)(char*, httpd_handle_t, int);

void initCamera(callb callback, void* camera_clnts, httpd_handle_t* server);

void cameraHandleCommand(char *str, httpd_handle_t handle, int fd);

#endif /* CONFIG_CAMERA */
#endif /* MAIN_CAMERA_USB_CAMERA_H_ */
