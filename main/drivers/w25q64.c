#include "w25q64.h"

#include <stdlib.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "W25Q64";

//spi设备句柄
static spi_device_handle_t s_spi_handle = NULL;


//====================内部函数====================

/**
 * @brief 读取状态寄存器 SR1
 * @return SR1 值（bit0=BUSY 忙标志，bit1=WEL 写使能锁存）
 */
static uint8_t w25q64_read_status(void)
{
    uint8_t tx[2] = { W25Q64_CMD_READ_STATUS1, 0x00 };
    uint8_t rx[2] = { 0 };

    spi_transaction_t trans = 
    {
        .length    = 8 * sizeof(tx),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(s_spi_handle, &trans);

    return rx[1];
}


/**
 * @brief 发送写使能命令，置位芯片内部 WEL 锁存
 * @note 每次页编程/扇区擦除前必须调用，否则芯片静默忽略写操作
 */
static void w25q64_write_enable(void)
{
    uint8_t cmd = W25Q64_CMD_WRITE_ENABLE;

        spi_transaction_t trans = 
        {
            .length    = 8,
            .tx_buffer = &cmd,
        };
        spi_device_polling_transmit(s_spi_handle, &trans);
}

/**
 * @brief 轮询等待芯片 BUSY 位清零
 * @note 页编程约 0.7~3ms，扇区擦除约 50~400ms；
 *       等待期间 vTaskDelay 让出 CPU，避免饿死其他任务
 */
static void w25q64_wait_busy(void)
{
    while (w25q64_read_status() & 0x01) 
    {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief 单次页编程（调用者保证不跨页，len <= 256）
 * @param addr 页内起始地址
 * @param data 待写数据
 * @param len  字节数（1~256）
 * @note 流程：Write Enable → 发命令+地址+数据 → 等 BUSY 清零
 */
static void w25q64_page_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    w25q64_write_enable();

    uint32_t total = 4 + len;
    uint8_t *tx = malloc(total);
    if (tx == NULL) {
        ESP_LOGE(TAG, "page_program malloc failed, %lu bytes", (unsigned long)total);
        return;
    }

    //------------------发送信息------------------//
    tx[0] = W25Q64_CMD_PAGE_PROGRAM;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >> 8) & 0xFF;
    tx[3] = addr & 0xFF;
    memcpy(tx + 4, data, len);

    spi_transaction_t trans = {
        .length    = total * 8,      //单位是bit，乘 8
        .tx_buffer = tx,
    };
    spi_device_polling_transmit(s_spi_handle, &trans);
    //------------------发送结束-----------------//

    free(tx);
    w25q64_wait_busy();
}

//====================对外接口====================

//擦除
 void w25q64_erase_sector(uint32_t addr)
 {
    addr &= ~(W25Q64_SECTOR_SIZE - 1);   //对齐到扇区首地址（0xFFFFF000）

    w25q64_write_enable();

    //拆解地址
    uint8_t tx[4] = 
    {
        W25Q64_CMD_SECTOR_ERASE,
        (addr >> 16) & 0xFF,
        (addr >> 8) & 0xFF,
        addr & 0xFF,
    };

    spi_transaction_t trans = 
    {
        .length    = 8 * sizeof(tx),
        .tx_buffer = tx,
    };
    spi_device_polling_transmit(s_spi_handle, &trans);

    w25q64_wait_busy();   // 擦除最慢，最长 400ms
 }

esp_err_t w25q64_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t remaining = len;

    //参数校验
    if (buf == NULL) 
        {
            ESP_LOGE(TAG, "write: buf is NULL");
            return ESP_ERR_INVALID_ARG;
        }
    if (len == 0) 
        {
            return ESP_OK;
        }
    if (addr + len > W25Q64_TOTAL_SIZE) 
        {
            ESP_LOGE(TAG, "write: out of range,addr=0x%06lX len=%lu",
                    (unsigned long)addr, (unsigned long)len);
            return ESP_ERR_INVALID_SIZE;
        }

    //跨页拆分
    while (remaining > 0) 
    {
        uint32_t page_offset = addr % W25Q64_PAGE_SIZE;
        uint32_t space_in_page = W25Q64_PAGE_SIZE - page_offset;
        uint32_t chunk = (remaining < space_in_page) ? remaining : space_in_page;

        w25q64_page_program(addr, buf, chunk);  //写入

        addr += chunk;      //下一页
        buf += chunk;       //写下一个数据
        remaining -= chunk; //算出还有多少没写
    }
    return ESP_OK;
}



esp_err_t w25q64_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t total = 4 + len; 

    //参数校验
    if(buf == NULL)
    {
        ESP_LOGE(TAG, "read: buf is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) 
    {
        return ESP_OK;               // 读 0 字节视为成功，直接返回
    }
    if (addr + len > W25Q64_TOTAL_SIZE)   //检查地址是否超出容量
    {
        ESP_LOGE(TAG, "read: out of range, addr=0x%06lX len=%lu",
        (unsigned long)addr, (unsigned long)len);
        return ESP_ERR_INVALID_SIZE;
    }


    //申请内存
    uint8_t *tx = malloc(total);
    uint8_t *rx = malloc(total);
    if (tx == NULL || rx == NULL) 
    {
        ESP_LOGE(TAG, "read malloc failed, %lu bytes",(unsigned long)total);
        free(tx);
        free(rx);
        return ESP_ERR_NO_MEM;
    }
    memset(tx , 0x00 , total);  //将地址后的发送数组清0

    //发送信息（指令 + 地址）
    tx[0] = W25Q64_CMD_READ_DATA;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >> 8) & 0xFF;
    tx[3] = addr & 0xFF;

    spi_transaction_t trans = 
    {
        .length    = total * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(s_spi_handle, &trans); //发送

    memcpy(buf, rx + 4, len);

    free(tx);
    free(rx);
    return ESP_OK;
}

uint32_t w25q64_read_id(void)
{
    //主机发送【详情见手册48页时序图】
    uint8_t tx[4] = { W25Q64_CMD_READ_JEDEC_ID, 0x00, 0x00, 0x00 }; 
    //接收从机返回的信息
    uint8_t rx[4] = { 0 };  //第一个返回值是无用的

    //发送
    spi_transaction_t trans = 
    {
    .length    = 8 * sizeof(tx),   //发4字节才 换3字节ID
    .tx_buffer = tx,
    .rx_buffer = rx,
    };
    spi_device_polling_transmit(s_spi_handle, &trans);

    return ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] <<8) | rx[3];

}

error_t w25q64_init(void)
{
    //配置总线
    spi_bus_config_t bus_cfg = 
    {
        .mosi_io_num = W25Q64_PIN_MOSI,    //输入
        .miso_io_num = W25Q64_PIN_MISO,    //输出
        .sclk_io_num = W25Q64_PIN_SCLK,    //时钟
        .quadwp_io_num = -1,               //不用四线 QSPI 的 WP（写保护），设为 -1
        .quadhd_io_num = -1,               //不用四线 QSPI 的 HD（保持），设为 -1
    };
    esp_err_t err = spi_bus_initialize(W25Q64_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed:%s", esp_err_to_name(err));
        return ERR_TIMEOUT;
    }


    //配置设备
    spi_device_interface_config_t devcfg = 
    {
        .clock_speed_hz = 20 * 1000 * 1000,  //20Mhz
        .mode           = 0,                 //CPOL=0 CPHA=0
        .spics_io_num   = W25Q64_PIN_CS,
        .queue_size     = 1,
    };
    err = spi_bus_add_device(W25Q64_SPI_HOST, &devcfg, &s_spi_handle);
        if (err != ESP_OK) 
        {
            ESP_LOGE(TAG, "spi_bus_add_device failed:%s", esp_err_to_name(err));
            return ERR_TIMEOUT;
        }
    

    //读取id
    uint32_t id = w25q64_read_id();
        if (id != W25Q64_JEDEC_ID) 
        {
        ESP_LOGE(TAG, "JEDEC ID mismatch: got 0x%06lX, expect 0x%06lX (check wiring)",
                      (unsigned long)id, (unsigned long)W25Q64_JEDEC_ID);
        return ERR_TIMEOUT;
        }

    ESP_LOGI(TAG, "W25Q64 ready, JEDEC ID: 0x%06lX, capacity: %dMB",
             (unsigned long)id, W25Q64_TOTAL_SIZE / 1024 / 1024);
    return ERR_OK;
}