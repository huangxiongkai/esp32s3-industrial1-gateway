#ifndef RS485_H
#define RS485_H
//ESP32-S3UART2RS485初始化配置
#include "driver/uart.h"    
#include "driver/gpio.h"    
#include <stdint.h>         

//引脚与参数定义
#define RS485_UART_PORT     UART_NUM_2        //使用 UART2 控制器
#define RS485_TX_PIN        GPIO_NUM_17       //ESP32 TX  → 模块 RXD
#define RS485_RX_PIN        GPIO_NUM_18       //ESP32 RX  ← 模块 TXD
#define RS485_EN_PIN        GPIO_NUM_7        //方向控制  → 模块 EN (DE/RE)

#define RS485_BAUD_RATE     9600              //波特率
#define RS485_DATA_BITS     UART_DATA_8_BITS  //数据位: 8
#define RS485_PARITY        UART_PARITY_DISABLE //校验: 无
#define RS485_STOP_BITS     UART_STOP_BITS_1  //停止位: 1

#define RS485_TX_TIMEOUT    100

#define RS485_TX_BUF_SIZE   256               //发送缓冲区(字节)
#define RS485_RX_BUF_SIZE   256               //接收缓冲区(字节)

    /**
     * @brief 初始化 RS485 驱动
     *        配置 UART2（9600-8N1）+ GPIO7
       @mode  输出模式：1  ； 输入模式：0
     */
    void rs485_init(void);

    /**
     * @brief 发送数据（半双工）
     *        流程：拉高 DE → 发送 → 等待移位寄存器空 → 拉低DEDE
     *
     * @param data 待发送数据指针
     * @param len  数据长度（字节）
     */
    void rs485_send(const uint8_t *data, uint16_t len);

    /**
     * @brief 接收数据（阻塞）
     * 
     * @param buf        接收缓冲区指针
     * @param buf_size   缓冲区大小（字节）
     * @param timeout_ms 超时时间（毫秒）
     * @return int       实际接收到的字节数，0 表示超时
     */
    int rs485_receive(uint8_t *buf, uint16_t buf_size, 
    uint32_t timeout_ms);

    /**
     * @brief 清空接收缓冲区
     *        发送请求前调用，防止读到上一轮的残留数据
     */
    void rs485_flush(void);
 

#endif