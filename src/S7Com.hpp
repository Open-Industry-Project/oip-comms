#pragma once

int S7_tag_create(const char* Hostname, const char* TagName);

int S7_tag_destroy(int identifier);

int S7_tag_write(int tag);

int S7_tag_read(int tag, int timeout);

int S7_tag_get_bit(int32_t tag);

int S7_tag_set_bit(int32_t tag, int val);

uint64_t S7_tag_get_uint64(int32_t tag);

int S7_tag_set_uint64(int32_t tag, uint64_t val);

int64_t S7_tag_get_int64(int32_t tag);

int S7_tag_set_int64(int32_t tag, int64_t val);

uint32_t S7_tag_get_uint32(int32_t tag);

int S7_tag_set_uint32(int32_t tag, uint32_t val);

int32_t S7_tag_get_int32(int32_t tag);

int S7_tag_set_int32(int32_t tag, int32_t val);

uint16_t S7_tag_get_uint16(int32_t tag);

int S7_tag_set_uint16(int32_t tag, uint16_t val);

int16_t S7_tag_get_int16(int32_t tag);

int S7_tag_set_int16(int32_t tag, int16_t val);

uint8_t S7_tag_get_uint8(int32_t tag);

int S7_tag_set_uint8(int32_t tag, uint8_t val);

int8_t S7_tag_get_int8(int32_t tag);

int S7_tag_set_int8(int32_t tag, int8_t val);

float S7_tag_get_float32(int32_t tag);

int S7_tag_set_float32(int32_t tag, float val);

// Direct declaration of Double are not supported in the M adress space
// only in DB. But mem copy can be made in the PLC.
double S7_tag_get_float64(int32_t tag);

int S7_tag_set_float64(int32_t tag, double val);

