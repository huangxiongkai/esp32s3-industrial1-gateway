#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include <stdint.h>
#include "types.h"
#include <stddef.h>

#define MODBUS_MAX_REG      16      //单次最多读 16 个寄存器
#define MODBUS_RESP_BUF     64      //响应缓冲区大小
#define MODBUS_TIMEOUT_MS   50     //从站应答超时

//RTU的3.5T帧间隔判定由UART硬件完成，不在软件层实现：
//rs485_init()里配置了uart_set_rx_timeout(4符号时间) + always_rx_timeout，
//软件只做整块读取和校验，避免FreeRTOS tick粒度问题（100Hz下<10ms超时会截成0）


//设备数据字段标识
typedef enum 
{
    FIELD_TEMP = 0,
    FIELD_HUMI,
    FIELD_CURRENT,
} modbus_field_t;


//设备读取结构体
typedef struct 
{
    uint8_t  slave_addr;              //Modbus 从站地址
    uint16_t reg_start;               //起始寄存器地址
    float    scale;                   //缩放因子（原始值 × scale = 实际值）
    modbus_field_t   field;           //映射对象
} modbus_reg_map_t;


error_t modbus_read_device(uint8_t dev_id, const modbus_reg_map_t *map
                              ,int map_count, device_data_t *out);


#endif
