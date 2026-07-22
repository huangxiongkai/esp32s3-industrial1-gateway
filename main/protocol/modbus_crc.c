#include "modbus_crc.h"


/**
 * @brief  计算 Modbus RTU 帧 CRC16 校验值（CRC-16/Modbus 标准）
 * @param  data 待校验数据首地址
 * @param  len  待校验数据长度（字节数）
 * @retval CRC16校验值，发送时低字节在前
*/
uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < len; i++) 
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) 
        {
            if (crc & 0x0001) 
            {
                crc = (crc >> 1) ^ 0xA001;
            } else 
             {
                crc >>= 1;
             }
        }
    }
        return crc;
}
