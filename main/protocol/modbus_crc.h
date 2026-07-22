#ifndef MODEBUS_CRC_H
#define MODEBUS_CRC_H

#include <stdint.h>

//crc16校验函数
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);

#endif