#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r2_ct_protocol.h"

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int test_crc_standard(void)
{
    static const uint8_t input[] = "123456789";
    return R2_CT_Crc32(input, 9U) == 0xCBF43926UL ? 0 : fail("CRC-32 golden vector");
}

static int test_encode_zero(void)
{
    static const uint8_t expected[] = {
        0xA5U,0x5AU,0x01U,0x01U,0x34U,0x12U,0x00U,0x00U,
        0x85U,0x62U,0xFDU,0x96U
    };
    uint8_t output[R2_CT_MAX_FRAME_BYTES];
    size_t size = R2_CT_EncodeFrame(R2_CT_TYPE_PING,0x1234U,NULL,0U,
        output,sizeof(output));
    if (size != sizeof(expected)) { return fail("zero frame length"); }
    return memcmp(output,expected,sizeof(expected)) == 0 ? 0 : fail("zero frame bytes");
}

static int test_encode_max(void)
{
    uint8_t payload[64];
    uint8_t output[R2_CT_MAX_FRAME_BYTES];
    size_t i;
    size_t size;

    for (i = 0U; i < sizeof(payload); ++i) { payload[i] = (uint8_t)i; }
    size = R2_CT_EncodeFrame(R2_CT_TYPE_PING,0x0201U,payload,64U,
        output,sizeof(output));
    if (size != 76U) { return fail("max frame length"); }
    if ((output[0] != 0xA5U) || (output[1] != 0x5AU) ||
        (output[6] != 0x40U) || (output[7] != 0x00U)) {
        return fail("max frame header");
    }
    if ((output[72] != 0x2FU) || (output[73] != 0x1FU) ||
        (output[74] != 0x32U) || (output[75] != 0xC9U)) {
        return fail("max frame CRC bytes");
    }
    return 0;
}

static int feed_frame(const uint8_t *bytes, size_t length, R2_CT_Frame *frame)
{
    R2_CT_Parser parser;
    size_t i;
    R2_CT_ParseStatus status = R2_CT_PARSE_NONE;
    R2_CT_ParserInit(&parser);
    for (i = 0U; i < length; ++i) {
        status = R2_CT_ParserPush(&parser,bytes[i],frame);
    }
    return status == R2_CT_PARSE_FRAME ? 0 : 1;
}

static int test_parse_fragmented(void)
{
    uint8_t payload[4] = {1U,2U,3U,4U};
    uint8_t bytes[R2_CT_MAX_FRAME_BYTES];
    R2_CT_Frame frame = {0};
    size_t size = R2_CT_EncodeFrame(R2_CT_TYPE_PING,0x00A1U,payload,4U,
        bytes,sizeof(bytes));
    if (feed_frame(bytes,size,&frame) != 0) { return fail("fragmented parser"); }
    if ((frame.type != R2_CT_TYPE_PING) || (frame.request_id != 0x00A1U) ||
        (frame.payload_length != 4U) || memcmp(frame.payload,payload,4U) != 0) {
        return fail("parsed frame fields");
    }
    return 0;
}

static int test_resync(void)
{
    uint8_t bytes[R2_CT_MAX_FRAME_BYTES];
    uint8_t stream[R2_CT_MAX_FRAME_BYTES + 5U];
    R2_CT_Parser parser;
    R2_CT_Frame frame = {0};
    R2_CT_ParseStatus status = R2_CT_PARSE_NONE;
    size_t size = R2_CT_EncodeFrame(R2_CT_TYPE_PING,7U,NULL,0U,bytes,sizeof(bytes));
    size_t i;

    stream[0]=0x00U; stream[1]=0xA5U; stream[2]=0x11U; stream[3]=0x22U; stream[4]=0x33U;
    memcpy(&stream[5],bytes,size);
    R2_CT_ParserInit(&parser);
    for (i=0U;i<size+5U;++i) { status=R2_CT_ParserPush(&parser,stream[i],&frame); }
    if (status != R2_CT_PARSE_FRAME) { return fail("resync did not recover"); }
    return frame.request_id == 7U ? 0 : fail("resync frame id");
}

static int test_bad_length(void)
{
    R2_CT_Parser parser;
    R2_CT_Frame frame = {0};
    static const uint8_t header[] = {0xA5U,0x5AU,1U,1U,1U,0U,65U,0U};
    size_t i;
    R2_CT_ParseStatus status = R2_CT_PARSE_NONE;
    R2_CT_ParserInit(&parser);
    for (i=0U;i<sizeof(header);++i) { status=R2_CT_ParserPush(&parser,header[i],&frame); }
    return status == R2_CT_PARSE_LENGTH_ERROR ? 0 : fail("length error not detected");
}

static int test_bad_crc(void)
{
    uint8_t bytes[R2_CT_MAX_FRAME_BYTES];
    R2_CT_Parser parser;
    R2_CT_Frame frame = {0};
    size_t size = R2_CT_EncodeFrame(R2_CT_TYPE_PING,1U,NULL,0U,bytes,sizeof(bytes));
    size_t i;
    R2_CT_ParseStatus status = R2_CT_PARSE_NONE;
    bytes[size-1U] ^= 1U;
    R2_CT_ParserInit(&parser);
    for (i=0U;i<size;++i) { status=R2_CT_ParserPush(&parser,bytes[i],&frame); }
    return status == R2_CT_PARSE_CRC_ERROR ? 0 : fail("CRC error not detected");
}

static int test_version(void)
{
    uint8_t bytes[R2_CT_MAX_FRAME_BYTES];
    R2_CT_Parser parser;
    R2_CT_Frame frame = {0};
    size_t size = R2_CT_EncodeFrame(R2_CT_TYPE_PING,1U,NULL,0U,bytes,sizeof(bytes));
    size_t i;
    R2_CT_ParseStatus status = R2_CT_PARSE_NONE;
    bytes[2] = 2U;
    {
        uint32_t crc = R2_CT_Crc32(bytes,size-4U);
        bytes[size-4U]=(uint8_t)crc;
        bytes[size-3U]=(uint8_t)(crc>>8);
        bytes[size-2U]=(uint8_t)(crc>>16);
        bytes[size-1U]=(uint8_t)(crc>>24);
    }
    R2_CT_ParserInit(&parser);
    for (i=0U;i<size;++i) { status=R2_CT_ParserPush(&parser,bytes[i],&frame); }
    return status == R2_CT_PARSE_VERSION_ERROR ? 0 : fail("version error not detected");
}

int main(int argc, char **argv)
{
    if (argc != 2) { return fail("expected one test case name"); }
    if (strcmp(argv[1],"crc_standard") == 0) { return test_crc_standard(); }
    if (strcmp(argv[1],"encode_zero") == 0) { return test_encode_zero(); }
    if (strcmp(argv[1],"encode_max") == 0) { return test_encode_max(); }
    if (strcmp(argv[1],"parse_fragmented") == 0) { return test_parse_fragmented(); }
    if (strcmp(argv[1],"resync") == 0) { return test_resync(); }
    if (strcmp(argv[1],"bad_length") == 0) { return test_bad_length(); }
    if (strcmp(argv[1],"bad_crc") == 0) { return test_bad_crc(); }
    if (strcmp(argv[1],"version") == 0) { return test_version(); }
    return fail("unknown test case");
}
