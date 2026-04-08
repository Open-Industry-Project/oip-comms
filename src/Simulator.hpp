#include <cstdint>

int Simulator_tag_create(const char* MathOperation);

int Simulator_tag_destroy(int identifier);

int Simulator_tag_read(int identifier);

int Simulator_tag_write(int identifier);

int Simulator_tag_get_bit(int identifier);

int Simulator_tag_set_bit(int identifier, int val);

uint64_t Simulator_tag_get_uint64(int identifier);

int Simulator_tag_set_uint64(int identifier, uint64_t val);

int64_t Simulator_tag_get_int64(int identifier);

int Simulator_tag_set_int64(int identifier, int64_t val);

uint32_t Simulator_tag_get_uint32(int identifier);

int Simulator_tag_set_uint32(int identifier, uint32_t val);

int32_t Simulator_tag_get_int32(int identifier);

int Simulator_tag_set_int32(int identifier, int32_t val);

uint16_t Simulator_tag_get_uint16(int identifier);

int Simulator_tag_set_uint16(int identifier, uint16_t val);

int16_t Simulator_tag_get_int16(int identifier);

int Simulator_tag_set_int16(int identifier, int16_t val);

uint8_t Simulator_tag_get_uint8(int identifier);

int Simulator_tag_set_uint8(int identifier, uint8_t val);

int8_t Simulator_tag_get_int8(int identifier);

int Simulator_tag_set_int8(int identifier, int8_t val);

double Simulator_tag_get_float64(int identifier);

int Simulator_tag_set_float64(int identifier, double val);

float Simulator_tag_get_float32(int identifier);

int Simulator_tag_set_float32(int identifier, float val);

