#ifndef R2_CT_PROTOCOL_H
#define R2_CT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define R2_CT_PROTOCOL_VERSION 1U
#define R2_CT_MAGIC0 0xA5U
#define R2_CT_MAGIC1 0x5AU
#define R2_CT_RUNTIME_MAX_PAYLOAD 64U
#define R2_CT_HEADER_BYTES 8U
#define R2_CT_CRC_BYTES 4U
#define R2_CT_MAX_FRAME_BYTES \
    (R2_CT_HEADER_BYTES + R2_CT_RUNTIME_MAX_PAYLOAD + R2_CT_CRC_BYTES)

#define R2_CT_TYPE_PING 0x01U
#define R2_CT_TYPE_PING_REPLY 0x81U

typedef struct
{
    uint8_t type;
    uint16_t request_id;
    uint16_t payload_length;
    uint8_t payload[R2_CT_RUNTIME_MAX_PAYLOAD];
} R2_CT_Frame;

typedef struct
{
    uint8_t bytes[R2_CT_MAX_FRAME_BYTES];
    uint16_t count;
    uint16_t expected_total;
} R2_CT_Parser;

typedef enum
{
    R2_CT_PARSE_NONE = 0,
    R2_CT_PARSE_FRAME,
    R2_CT_PARSE_LENGTH_ERROR,
    R2_CT_PARSE_CRC_ERROR,
    R2_CT_PARSE_VERSION_ERROR
} R2_CT_ParseStatus;

uint32_t R2_CT_Crc32(const uint8_t *data, size_t length);
void R2_CT_ParserInit(R2_CT_Parser *parser);
R2_CT_ParseStatus R2_CT_ParserPush(
    R2_CT_Parser *parser,
    uint8_t byte,
    R2_CT_Frame *frame);
size_t R2_CT_EncodeFrame(
    uint8_t type,
    uint16_t request_id,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_capacity);

#endif
