#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <stdint.h>
#include <assert.h>
#include "types.h"

//====================分区布局（W25Q64共8MB）====================
#define CACHE_START_ADDR    0x000000     // 缓存区起点
#define CACHE_END_ADDR      0x800000     // 缓存区终点：8MB 芯片末尾

//====================条目定义====================
#define CACHE_ENTRY_SIZE    256                  // = W25Q64 一页大小
#define CACHE_DATA_MAX      244                  //payload 最大长度
//最大存储条数（该页总大小/JSON所占字节数）
#define CACHE_MAX_ENTRIES   ((CACHE_END_ADDR - CACHE_START_ADDR) / CACHE_ENTRY_SIZE)  

#define MAGIC_VALID         0xDEADBEEF   //待补传
#define MAGIC_SENT          0x00000000   //已补传（逻辑删除，纯 1→0 可直接擦除）
#define MAGIC_BLANK         0xFFFFFFFF   //空白（擦除态）

/**
 * @brief 缓存条目，恰好 256 字节（一个 Flash 页）
 */
typedef struct {
    uint32_t magic;                  //条目状态
    uint32_t timestamp;              //写入时刻
    uint16_t data_len;               //data有效长度
    uint16_t reserved;               //对齐占位
    uint8_t  data[CACHE_DATA_MAX];   //JSON数据大小
} cache_entry_t;
//检验页面大小有无被更改
static_assert(sizeof(cache_entry_t) == CACHE_ENTRY_SIZE,
               "cache_entry_t must equal one flash page");


/**
 * @brief 初始化存储层：w25q64_init → 建 Mutex → 读 NVS 溢出计数 → 扫描重建读写指针
 * @return ERR_OK 成功；ERR_TIMEOUT Flash 初始化失败
 */
error_t flash_storage_init(void);

/**
 * @brief 写入一条数据
 * @param data payload 指针
 * @param len  payload 长度，必须 <= CACHE_DATA_MAX
 * @return 0 成功；-1 加锁超时；-2 数据超长
 * @note 缓存满时自动覆盖最旧扇区（16 条），溢出计数器 +16，不会返回失败
 */
int flash_storage_write(const uint8_t *data, uint16_t len);

/**
 * @brief 读取最旧一条待补传数据（不推进读指针，配合 mark_sent 使用）
 * @param buf      接收缓冲区
 * @param buf_size 缓冲区大小
 * @param out_len  [out] 实际数据长度
 * @param out_ts   [out] 写入时间戳
 * @return 0 成功；-1 缓存空或加锁超时
 */
int flash_storage_read(uint8_t *buf, uint16_t buf_size,
                       uint16_t *out_len, uint32_t *out_ts);

/**
 * @brief 确认最旧一条已补传成功：magic 改写为 SENT + 读指针推进
 * @note 必须在 flash_storage_read + 发布成功之后调用
 */
void flash_storage_mark_sent(void);

/**
 * @brief 查询待补传条数
 * @return 待补传条数（供 M9 LCD 显示"离线缓存 N 条"）
 */
int flash_storage_pending_count(void);

/**
 * @brief 取出溢出计数并归零（RAM + NVS 同步清零）
 * @return 自上次调用以来因缓存满被覆盖的条目总数
 * @note cache_task 恢复在线后调用，用于上报 overflow_count 物模型属性
 */
uint32_t flash_storage_take_overflow(void);



/**
 * @brief 归还溢出计数（take_overflow 后上报失败时调用）
 * @param count 要还回去的数量
 */
void flash_storage_giveback_overflow(uint32_t count);


#endif