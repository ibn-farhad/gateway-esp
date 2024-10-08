/*
 * printer.h
 *
 *  Created on: Aug 9, 2023
 *      Author: markscheider
 */

#ifndef MAIN_PRINTER_ESC_H_
#define MAIN_PRINTER_ESC_H_
#include "sdkconfig.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "mbedtls/base64.h"
#include "usb/usb_host.h"

#include <stdbool.h>
#include <string.h>
#include <ctype.h>

//#include "mbedtls/base64.h"
//#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "freertos/semphr.h"

//#include "esp_intr_alloc.h"
//#include "esp_log.h"
//#include "usb/usb_host.h"

#define DAEMON_TASK_PRIORITY    2
#define CLASS_TASK_PRIORITY     3

#define CLASS_DRIVER_ACTION_OPEN_DEV    0x01
#define CLASS_DRIVER_ACTION_TRANSFER    0x02
#define CLASS_DRIVER_ACTION_CLOSE_DEV   0x04

//  BirdSimurg, Governess //
//#define EndPointAddressIn 0x81
//#define EndPointAddressOut 0x03

/** SR **/
//#define EndPointAddressIn 0x82
//#define EndPointAddressOut 0x01

// ASCII codes used by some of the printer config commands:
#define ASCII_TAB '\t' //!< Horizontal tab
#define ASCII_LF '\n'  //!< Line feed
#define ASCII_FF '\f'  //!< Form feed
#define ASCII_CR '\r'  //!< Carriage return
#define ASCII_DC2 18   //!< Device control 2
#define ASCII_ESC 27   //!< Escape
#define ASCII_FS 28    //!< Field separator
#define ASCII_GS 29    //!< Group separator

// === Character commands ===
#define FONT_MASK (1 << 0) //!< Select character font A or B
#define INVERSE_MASK                                                           \
  (1 << 1) //!< Turn on/off white/black reverse printing mode. Not in 2.6.8
           //!< firmware (see inverseOn())
#define UPDOWN_MASK (1 << 2)        //!< Turn on/off upside-down printing mode
#define BOLD_MASK (1 << 3)          //!< Turn on/off bold printing mode
#define DOUBLE_HEIGHT_MASK (1 << 4) //!< Turn on/off double-height printing mode
#define DOUBLE_WIDTH_MASK (1 << 5)  //!< Turn on/off double-width printing mode
#define STRIKE_MASK (1 << 6)        //!< Turn on/off deleteline mode


typedef void (*callb)(char*, httpd_handle_t, int); //(char *mes, httpd_handle_t handle, int device)


void initESC(callb callback);
void escHandleCommand(char *str, httpd_handle_t handle, int fd);
void class_driver_task(void *arg);
void host_lib_daemon_task(void *arg);
void print();
void writeByte(uint8_t a);
void print_image(int width, int height, const char* alignment, unsigned  char* image);
void print_text(const char* data, const char* size, bool bold, bool underline, const char* alignment, int line_spacing , char font );
void partial_cut();
void feed(uint8_t x);
void setBarcodeHeight(uint8_t val);
void printBarcode(const char *text, uint8_t type);
void getPrinterStatTask(void *arg);
uint8_t getPrinterStat(uint8_t status_code);
// Internal character sets used with ESC R n

#endif /* MAIN_PRINTER_ESC_H_ */
