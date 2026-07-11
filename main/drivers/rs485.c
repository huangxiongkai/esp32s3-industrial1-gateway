#include "rs485.h"
#include "driver/uart.h"    
#include "driver/gpio.h"    
#include "esp_log.h"        
#include "freertos/FreeRTOS.h"  
#include <stdint.h>

void rs485_init(void)
{
    const uart_config_t uart_config = 
    {
        .baud_rate  = RS485_BAUD_RATE,          // 9600
        .data_bits  = RS485_DATA_BITS,          // 8 位
        .parity     = RS485_PARITY,             // 无校验
        .stop_bits  = RS485_STOP_BITS,          // 1 位停止位
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE, // 无硬件流控
        .source_clk = UART_SCLK_APB,            // APB 80MHz
    };
    uart_param_config(RS485_UART_PORT, &uart_config);

    //  TX/RX 引脚配置
    uart_set_pin(RS485_UART_PORT,
                RS485_TX_PIN,           // TX  → GPIO17 → 模块 RXD
                RS485_RX_PIN,           // RX  → GPIO18 ← 模块 TXD
                UART_PIN_NO_CHANGE,     // RTS 不用  [请求发送]
                UART_PIN_NO_CHANGE);   // CTS 不用   [允许发送]

    //  安装驱动，分配收发缓冲区
    uart_driver_install(RS485_UART_PORT,
                        RS485_RX_BUF_SIZE,      // RX 缓冲区
                        0,                      // TX 缓冲区
                        0, NULL, 0);    

    //  DE引脚配置
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RS485_EN_PIN),  // 选中 GPIO7
        .mode         = GPIO_MODE_OUTPUT,         // 输出模式
        .pull_up_en   = GPIO_PULLUP_DISABLE,      // 不需要内部上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,    // 不需要内部下拉
        .intr_type    = GPIO_INTR_DISABLE,        // 不需要中断
    };
    gpio_config(&io_conf);

    //默认拉低（接收模式）
    gpio_set_level(RS485_EN_PIN, 0);    
}

void rs485_send(const uint8_t *data , uint16_t len)
{
    if (data == NULL ||  len == 0)
    return;
    
    //切为发送模式
    gpio_set_level(RS485_EN_PIN , 1);
    
    //写入数据
    int bytes_written = uart_write_bytes(UART_NUM_2 ,data , len);
    //等待发完（超时 100ms）
    if (bytes_written > 0)
    {
        uart_wait_tx_done(UART_NUM_2,
                          pdMS_TO_TICKS(RS485_TX_TIMEOUT));
    }
    //切回接收模式
    gpio_set_level(RS485_EN_PIN , 0);
}

int rs485_receive(uint8_t *buf, uint16_t buf_size,uint32_t timeout_ms)
{
    //返回实际接收字节数
    int receive_bytes = uart_read_bytes(UART_NUM_2 ,buf , buf_size , pdMS_TO_TICKS(timeout_ms));
    return receive_bytes;
}

void rs485_flush(void)
{
    //清空接收 FIFO
    int flush_input = uart_flush_input(UART_NUM_2);

}