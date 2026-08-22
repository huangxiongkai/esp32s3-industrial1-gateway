#ifndef W25Q64_H
#define W25Q64_H    

#include "driver/spi_master.h"
#include "types.h"
#include <stdint.h>
#include "esp_err.h"


//====================引脚定义====================
#define W25Q64_PIN_CS       9
#define W25Q64_PIN_SCLK     47
#define W25Q64_PIN_MOSI     48
#define W25Q64_PIN_MISO     14
#define W25Q64_SPI_HOST     SPI3_HOST

//====================命令码（W25Q64数据手册）====================
#define W25Q64_CMD_WRITE_ENABLE     0x06
#define W25Q64_CMD_READ_DATA        0x03    //
#define W25Q64_CMD_PAGE_PROGRAM     0x02
#define W25Q64_CMD_SECTOR_ERASE     0x20
#define W25Q64_CMD_CHIP_ERASE       0xC7
#define W25Q64_CMD_READ_STATUS1     0x05
#define W25Q64_CMD_READ_JEDEC_ID    0x9F

//====================芯片参数====================
#define W25Q64_JEDEC_ID             0xEF4017
#define W25Q64_PAGE_SIZE            256
#define W25Q64_SECTOR_SIZE          4096
#define W25Q64_TOTAL_SIZE           (8 * 1024 * 1024)  // 8MB


/**
 * @brief 初始化 W25Q64：配置 SPI 总线 + 挂载设备 + 读ID 验证
* @return ERR_OK 成功；ERR_TIMEOUT 读 ID失败（接线错误）
* @note 必须在 flash_storage_init 之前调用
*/
error_t w25q64_init(void);

/**
 * @brief 从指定地址连续读取数据
 * @param addr 起始地址（0 ~ 8MB-1）
 * @param buf  接收缓冲区
 * @param len  读取字节数
 */
esp_err_t w25q64_read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief 向指定地址写入数据（自动处理跨页拆分）
 * @param addr 起始地址
 * @param buf  待写入数据
 * @param len  写入字节数
 * @note 调用前必须确保目标区域已擦除（全 0xFF）
 */
esp_err_t w25q64_write(uint32_t addr, const uint8_t *buf,
uint32_t len);

/**
 * @brief 擦除 addr 所在的 4KB 扇区
 * @param addr
扇区内任意地址（内部自动对齐到扇区首地址）
 * @note 擦除耗时约 50~400ms，内部会忙等待
*/
void w25q64_erase_sector(uint32_t addr);

/**
 * @brief 读取 JEDEC ID（用于硬件验证）
 * @return 3 字节 ID（正常为 0xEF4017）
 */
uint32_t w25q64_read_id(void);



#endif