#include "r2_ct_protocol.h"

#include <string.h>

static uint16_t ReadU16Le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t ReadU32Le(const uint8_t *p)
{
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static void WriteU16Le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void WriteU32Le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
    p[2] = (uint8_t)((value >> 16) & 0xFFU);
    p[3] = (uint8_t)((value >> 24) & 0xFFU);
}

uint32_t R2_CT_Crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    size_t i;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }

    for (i = 0U; i < length; ++i)
    {
        uint32_t bit;
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
        {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

void R2_CT_ParserInit(R2_CT_Parser *parser)
{
    if (parser != NULL)
    {
        memset(parser, 0, sizeof(*parser));
    }
}

static void ParserResetWithPossibleMagic(R2_CT_Parser *parser, uint8_t byte)
{
    parser->count = 0U;
    parser->expected_total = 0U;
    if (byte == R2_CT_MAGIC0)
    {
        parser->bytes[0] = byte;
        parser->count = 1U;
    }
}

R2_CT_ParseStatus R2_CT_ParserPush(
    R2_CT_Parser *parser,
    uint8_t byte,
    R2_CT_Frame *frame)
{
    uint16_t payload_length;
    uint16_t total;
    uint32_t observed_crc;
    uint32_t expected_crc;

    if ((parser == NULL) || (frame == NULL))
    {
        return R2_CT_PARSE_LENGTH_ERROR;
    }

    if (parser->count == 0U)
    {
        if (byte == R2_CT_MAGIC0)
        {
            parser->bytes[0] = byte;
            parser->count = 1U;
        }
        return R2_CT_PARSE_NONE;
    }

    if (parser->count == 1U)
    {
        if (byte == R2_CT_MAGIC1)
        {
            parser->bytes[1] = byte;
            parser->count = 2U;
        }
        else
        {
            ParserResetWithPossibleMagic(parser, byte);
        }
        return R2_CT_PARSE_NONE;
    }

    if (parser->count >= R2_CT_MAX_FRAME_BYTES)
    {
        ParserResetWithPossibleMagic(parser, byte);
        return R2_CT_PARSE_LENGTH_ERROR;
    }

    parser->bytes[parser->count++] = byte;

    if (parser->count == R2_CT_HEADER_BYTES)
    {
        payload_length = ReadU16Le(&parser->bytes[6]);
        if (payload_length > R2_CT_RUNTIME_MAX_PAYLOAD)
        {
            R2_CT_ParserInit(parser);
            return R2_CT_PARSE_LENGTH_ERROR;
        }
        parser->expected_total =
            (uint16_t)(R2_CT_HEADER_BYTES + payload_length + R2_CT_CRC_BYTES);
    }

    if ((parser->expected_total == 0U) ||
        (parser->count < parser->expected_total))
    {
        return R2_CT_PARSE_NONE;
    }

    total = parser->expected_total;
    observed_crc = ReadU32Le(&parser->bytes[total - R2_CT_CRC_BYTES]);
    expected_crc = R2_CT_Crc32(parser->bytes, total - R2_CT_CRC_BYTES);

    if (observed_crc != expected_crc)
    {
        R2_CT_ParserInit(parser);
        return R2_CT_PARSE_CRC_ERROR;
    }

    if (parser->bytes[2] != R2_CT_PROTOCOL_VERSION)
    {
        R2_CT_ParserInit(parser);
        return R2_CT_PARSE_VERSION_ERROR;
    }

    frame->type = parser->bytes[3];
    frame->request_id = ReadU16Le(&parser->bytes[4]);
    frame->payload_length = ReadU16Le(&parser->bytes[6]);
    if (frame->payload_length != 0U)
    {
        memcpy(frame->payload, &parser->bytes[R2_CT_HEADER_BYTES],
            frame->payload_length);
    }

    R2_CT_ParserInit(parser);
    return R2_CT_PARSE_FRAME;
}

size_t R2_CT_EncodeFrame(
    uint8_t type,
    uint16_t request_id,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_capacity)
{
    size_t total;
    uint32_t crc;

    if ((output == NULL) ||
        (payload_length > R2_CT_RUNTIME_MAX_PAYLOAD) ||
        ((payload_length != 0U) && (payload == NULL)))
    {
        return 0U;
    }

    total = (size_t)R2_CT_HEADER_BYTES + payload_length + R2_CT_CRC_BYTES;
    if (output_capacity < total)
    {
        return 0U;
    }

    output[0] = R2_CT_MAGIC0;
    output[1] = R2_CT_MAGIC1;
    output[2] = R2_CT_PROTOCOL_VERSION;
    output[3] = type;
    WriteU16Le(&output[4], request_id);
    WriteU16Le(&output[6], payload_length);
    if (payload_length != 0U)
    {
        memcpy(&output[R2_CT_HEADER_BYTES], payload, payload_length);
    }

    crc = R2_CT_Crc32(output, total - R2_CT_CRC_BYTES);
    WriteU32Le(&output[total - R2_CT_CRC_BYTES], crc);
    return total;
}
