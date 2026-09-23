#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIND_USB_MAGIC "WINDSC01"
#define WIND_USB_MAGIC_SIZE 8u
#define WIND_USB_PROTOCOL_VERSION 1u
#define WIND_USB_HEADER_SIZE 24u
#define WIND_USB_MAX_PAYLOAD 16384u
#define WIND_USB_MAX_FRAME_SIZE (WIND_USB_HEADER_SIZE + WIND_USB_MAX_PAYLOAD)

typedef enum {
    WIND_USB_MESSAGE_REQUEST = 1,
    WIND_USB_MESSAGE_RESULT = 2,
    WIND_USB_MESSAGE_ERROR = 3,
} wind_usb_message_type_t;

typedef struct {
    uint16_t message_type;
    uint32_t request_id;
    const uint8_t *payload;
    size_t payload_length;
} wind_usb_frame_t;

typedef void (*wind_usb_frame_callback_t)(const wind_usb_frame_t *frame, void *context);

typedef struct {
    uint8_t buffer[WIND_USB_MAX_FRAME_SIZE];
    size_t length;
    uint32_t last_request_id;
    bool has_last_request;
} wind_usb_parser_t;

void wind_usb_parser_init(wind_usb_parser_t *parser);
esp_err_t wind_usb_parser_feed(wind_usb_parser_t *parser, const uint8_t *bytes, size_t length,
                               wind_usb_frame_callback_t callback, void *context);
size_t wind_usb_encode_frame(uint32_t request_id, uint16_t message_type, const uint8_t *payload,
                             size_t payload_length, uint8_t *output, size_t output_size);
uint32_t wind_usb_crc32(const uint8_t *bytes, size_t length);
void wind_usb_write_u16(uint8_t *output, uint16_t value);
void wind_usb_write_u32(uint8_t *output, uint32_t value);

#ifdef __cplusplus
}
#endif
