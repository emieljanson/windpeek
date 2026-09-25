#include "wind_usb_protocol.h"

#include <stdbool.h>
#include <string.h>

#include "cJSON.h"

static uint16_t read_u16(const uint8_t *input)
{
    return (uint16_t) input[0] | ((uint16_t) input[1] << 8);
}

static uint32_t read_u32(const uint8_t *input)
{
    return (uint32_t) input[0] | ((uint32_t) input[1] << 8) |
           ((uint32_t) input[2] << 16) | ((uint32_t) input[3] << 24);
}

void wind_usb_write_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t) value;
    output[1] = (uint8_t) (value >> 8);
}

void wind_usb_write_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t) value;
    output[1] = (uint8_t) (value >> 8);
    output[2] = (uint8_t) (value >> 16);
    output[3] = (uint8_t) (value >> 24);
}

uint32_t wind_usb_crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (uint32_t) -(int32_t) (crc & 1));
        }
    }
    return ~crc;
}

void wind_usb_parser_init(wind_usb_parser_t *parser)
{
    if (parser) memset(parser, 0, sizeof(*parser));
}

bool wind_usb_parser_expire_partial(wind_usb_parser_t *parser, uint64_t idle_ms)
{
    if (!parser || !parser->length || idle_ms < WIND_USB_PARTIAL_TIMEOUT_MS) return false;
    parser->length = 0;
    return true;
}

static void discard_prefix(wind_usb_parser_t *parser, size_t count)
{
    if (count >= parser->length) {
        parser->length = 0;
        return;
    }
    memmove(parser->buffer, parser->buffer + count, parser->length - count);
    parser->length -= count;
}

static void seek_magic(wind_usb_parser_t *parser)
{
    while (parser->length >= WIND_USB_MAGIC_SIZE &&
           memcmp(parser->buffer, WIND_USB_MAGIC, WIND_USB_MAGIC_SIZE) != 0) {
        discard_prefix(parser, 1);
    }
}

static bool starts_new_session(uint32_t request_id, uint16_t message_type,
                               const uint8_t *payload, size_t length)
{
    if (request_id != 1 || message_type != WIND_USB_MESSAGE_REQUEST || !payload) return false;
    cJSON *json = cJSON_ParseWithLength((const char *) payload, length);
    const cJSON *command = json ? cJSON_GetObjectItemCaseSensitive(json, "command") : NULL;
    const bool is_hello = cJSON_IsObject(json) && cJSON_IsString(command) &&
                          command->valuestring && strcmp(command->valuestring, "hello") == 0;
    cJSON_Delete(json);
    return is_hello;
}

wind_usb_feed_result_t wind_usb_parser_feed(wind_usb_parser_t *parser, const uint8_t *bytes, size_t length,
                               wind_usb_frame_callback_t callback, void *context)
{
    if (!parser || (!bytes && length > 0) || !callback)
        return (wind_usb_feed_result_t){.error = ESP_ERR_INVALID_ARG};
    wind_usb_feed_result_t result = {.error = ESP_OK};
    for (size_t input = 0; input < length; ++input) {
        if (parser->length == sizeof(parser->buffer)) {
            parser->length = 0; // Framing errors must preserve replay protection.
            result.error = ESP_ERR_INVALID_SIZE;
        }
        parser->buffer[parser->length++] = bytes[input];
        seek_magic(parser);
        while (parser->length >= WIND_USB_HEADER_SIZE) {
            if (read_u16(parser->buffer + 8) != WIND_USB_PROTOCOL_VERSION) {
                discard_prefix(parser, WIND_USB_MAGIC_SIZE);
                result.error = ESP_ERR_NOT_SUPPORTED;
                seek_magic(parser);
                continue;
            }
            const uint32_t payload_length = read_u32(parser->buffer + 16);
            if (payload_length > WIND_USB_MAX_PAYLOAD) {
                parser->length = 0;
                result.error = ESP_ERR_INVALID_SIZE;
                continue;
            }
            const size_t frame_size = WIND_USB_HEADER_SIZE + payload_length;
            if (parser->length < frame_size) break;
            const uint32_t expected_crc = read_u32(parser->buffer + 20);
            const uint32_t actual_crc = wind_usb_crc32(parser->buffer + WIND_USB_HEADER_SIZE,
                                                       payload_length);
            if (expected_crc != actual_crc) {
                discard_prefix(parser, frame_size);
                result.error = ESP_ERR_INVALID_CRC;
                seek_magic(parser);
                continue;
            }
            const uint32_t request_id = read_u32(parser->buffer + 12);
            const uint16_t message_type = read_u16(parser->buffer + 10);
            if (starts_new_session(request_id, message_type,
                                   parser->buffer + WIND_USB_HEADER_SIZE, payload_length)) {
                parser->has_last_request = false;
            }
            if (!parser->has_last_request || request_id > parser->last_request_id) {
                const wind_usb_frame_t frame = {
                    .message_type = message_type,
                    .request_id = request_id,
                    .payload = parser->buffer + WIND_USB_HEADER_SIZE,
                    .payload_length = payload_length,
                };
                callback(&frame, context);
                ++result.delivered_frames;
                parser->last_request_id = request_id;
                parser->has_last_request = true;
            }
            discard_prefix(parser, frame_size);
            seek_magic(parser);
        }
    }
    return result;
}

size_t wind_usb_encode_frame(uint32_t request_id, uint16_t message_type, const uint8_t *payload,
                             size_t payload_length, uint8_t *output, size_t output_size)
{
    if ((!payload && payload_length > 0) || !output || payload_length > WIND_USB_MAX_PAYLOAD ||
        output_size < WIND_USB_HEADER_SIZE + payload_length) return 0;
    memcpy(output, WIND_USB_MAGIC, WIND_USB_MAGIC_SIZE);
    wind_usb_write_u16(output + 8, WIND_USB_PROTOCOL_VERSION);
    wind_usb_write_u16(output + 10, message_type);
    wind_usb_write_u32(output + 12, request_id);
    wind_usb_write_u32(output + 16, (uint32_t) payload_length);
    wind_usb_write_u32(output + 20, wind_usb_crc32(payload, payload_length));
    if (payload_length > 0) memcpy(output + WIND_USB_HEADER_SIZE, payload, payload_length);
    return WIND_USB_HEADER_SIZE + payload_length;
}
