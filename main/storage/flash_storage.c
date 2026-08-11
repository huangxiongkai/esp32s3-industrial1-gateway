#include "flash_storage.h"
#include "w25q64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "FLASH_STORAGE";
#define NVS_NAMESPACE "flash_cache"       // NVS 命名空间

static SemaphoreHandle_t s_mutex = NULL;
//读写指针起始地址
static uint32_t s_read_addr = CACHE_START_ADDR;
static uint32_t s_write_addr = CACHE_START_ADDR;
//有效条目（找到的待上传数据数目）
static int s_entry_count = 0;
//溢出计数器（每满 16 条，+16）
static uint32_t s_overflow_count = 0;
//NVS 句柄
static nvs_handle_t s_nvs = 0;

//-----------------------------------------------------------//

/**
 * @brief 归还溢出计数（take_overflow 后上报失败时调用）
 * @param count 要还回去的数量
 * @note 与 take_overflow 镜像对称：加锁 → 累加 → 持久化
 */
void flash_storage_giveback_overflow(uint32_t count)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "giveback lock timeout");
        return;
    }

    s_overflow_count += count;
    overflow_persist();

    xSemaphoreGive(s_mutex);
}

/**
 * @brief publish成功后，修改magic状态并更新读指针
 * @note 必须在 flash_storage_read + 发布成功之后调用
 */
void flash_storage_mark_sent(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "mark_sent lock timeout");
        return;
    }
    //更新状态为已发送
    uint32_t magic = MAGIC_SENT;
    w25q64_write(s_read_addr, (uint8_t*)&magic, sizeof(magic));
    
    s_read_addr += CACHE_ENTRY_SIZE;
    if (s_read_addr >= CACHE_END_ADDR)
    {
        s_read_addr = CACHE_START_ADDR;
    }

    //发布成功则有效条目数减一
    s_entry_count--;

    xSemaphoreGive(s_mutex);
}

/**
 * @brief 持久化溢出计数到 NVS
 * @note 满溢时调用，保证掉电不丢
 */
static void overflow_persist(void)
{
    esp_err_t err = nvs_set_u32(s_nvs, "ovf_cnt", s_overflow_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u32 failed: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
}

/**
 * @brief 读取最旧一条待补传数据
 * @param buf      接收缓冲区
 * @param buf_size 缓冲区大小
 * @param out_len  [out] 实际数据长度
 * @param out_ts   [out] 写入时间戳
 * @return 0 成功；-1 缓存空或加锁超时
 * @note 正常读取时不推进读指针（留给 mark_sent）；
    *    遇到损坏条目时自愈跳过（推进读指针 + count--）
 */
int flash_storage_read(uint8_t *buf, uint16_t buf_size,
                       uint16_t *out_len, uint32_t *out_ts)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "read lock timeout");
        return -1;
    }

    while (s_entry_count > 0)
    {
        cache_entry_t entry = {0};
        w25q64_read(s_read_addr, (uint8_t*)&entry, CACHE_ENTRY_SIZE);

        //参数校验
        if (entry.magic == MAGIC_VALID && entry.data_len <= CACHE_DATA_MAX )
        {
            //检查缓冲区大小
            if (entry.data_len <= buf_size) 
            {
                memcpy(buf, entry.data, entry.data_len);
                *out_len = entry.data_len;
                *out_ts = entry.timestamp;
                xSemaphoreGive(s_mutex);
                return 0;
            }
        }
        else
        {
            uint32_t magic_sent = MAGIC_SENT;
            w25q64_write(s_read_addr, (uint8_t*)&magic_sent, sizeof(magic_sent));
            ESP_LOGW(TAG, "corrupt entry skipped at 0x%06lX", s_read_addr);
        }

        //读指针移动
        s_read_addr += CACHE_ENTRY_SIZE;
        if (s_read_addr >= CACHE_END_ADDR)
        {
            s_read_addr = CACHE_START_ADDR;
        }
        s_entry_count--;
    }
    xSemaphoreGive(s_mutex);

    //缓存为空
    return -1;
}

/**
 * @brief 写入一条数据
 * @param data payload 指针
 * @param len  payload 长度，必须 <= CACHE_DATA_MAX
 * @return 0 成功；-1 加锁超时；-2 数据超长
 * @note 缓存满时自动覆盖最旧扇区（16 条），溢出计数器 +16，不会返回失败
 */
int flash_storage_write(const uint8_t *data, uint16_t len)
{
    //1.加锁（扇区擦除最长400ms）
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "write lock timeout");
        return -1;
    }

    //2.长度检查 
    if (len > CACHE_DATA_MAX)
    {
        xSemaphoreGive(s_mutex);
        return -2;
    }

    //3.满溢处理，缓存已满，擦除写指针所在扇区，丢弃16条最旧数据 
    if (s_entry_count == CACHE_MAX_ENTRIES)
    {
        uint32_t sector = s_write_addr & ~(W25Q64_SECTOR_SIZE - 1);
        w25q64_erase_sector(sector);

        s_read_addr = sector + W25Q64_SECTOR_SIZE;
        if (s_read_addr >= CACHE_END_ADDR)
        {
            s_read_addr = CACHE_START_ADDR;
        }
        //页数
        uint16_t entries_erased = W25Q64_SECTOR_SIZE / CACHE_ENTRY_SIZE;
        s_entry_count -= entries_erased;
        s_overflow_count += entries_erased;
        overflow_persist();

        ESP_LOGW(TAG, "cache full, overwrote %u oldest, overflow=%lu",
                 entries_erased, s_overflow_count);
    }
    //4.回绕擦除，写指针回到扇区首
    else if (s_write_addr % W25Q64_SECTOR_SIZE == 0)
    {
        w25q64_erase_sector(s_write_addr);
    }

    //5.构造结构体准备写入
    cache_entry_t entry = {0};
    entry.magic = MAGIC_VALID;
    entry.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    entry.data_len = len;
    memcpy(entry.data, data, len);

    //6.写入Flash
    w25q64_write(s_write_addr, (uint8_t*)&entry, CACHE_ENTRY_SIZE);

    //7.推进写指针
    s_write_addr += CACHE_ENTRY_SIZE;
    if (s_write_addr >= CACHE_END_ADDR)
    {
        s_write_addr = CACHE_START_ADDR;
    }

    //8.计数加一
    s_entry_count++;

    //9.解锁
    xSemaphoreGive(s_mutex);
    return 0;
}

error_t flash_storage_init(void)
{
    // 1. 硬件初始化
    if (w25q64_init() != ERR_OK) 
    {
        ESP_LOGE(TAG, "w25q64_init failed");
        return ERR_TIMEOUT;
    }

    //2.创建互斥锁
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) 
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ERR_TIMEOUT;   // error_t 无通用失败码，暂用 ERR_TIMEOUT 占位
    }

    //3.读取NVS的溢出计数
    esp_err_t esp_err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(esp_err));
        return ERR_NVS;
    }
    
    uint32_t ovf = 0;
    esp_err = nvs_get_u32(s_nvs, "ovf_cnt", &ovf);
    if (esp_err == ESP_ERR_NVS_NOT_FOUND) 
    {
        //首次运行
        s_overflow_count = 0;
    } 
    else if (esp_err != ESP_OK) 
    {
        ESP_LOGE(TAG, "nvs_get_u32 failed: %s", esp_err_to_name(esp_err));
        return ERR_NVS;
    } 
    else 
    {
        s_overflow_count = ovf;
    }

     // 4. 扫描 Flash 重建读写指针
    uint32_t addr = CACHE_START_ADDR;
    bool first_valid_found = false;   //待上传数据标志位（代表有没有找到第一个待上传数据）
    s_entry_count = 0;                //有效条目清零
    
    while (addr < CACHE_END_ADDR) 
    {
        //读取magic字段
        uint32_t magic;
        if (w25q64_read(addr, (uint8_t*)&magic, sizeof(magic)) != ESP_OK) 
        {
            ESP_LOGE(TAG, "scan read failed at 0x%06lX", addr);
            s_write_addr = addr;  //读取失败，设置 write_addr
            goto done;
        }
        
        if (magic == MAGIC_BLANK) 
        {
            s_write_addr = addr;
            break;
        } 
        else if (magic == MAGIC_VALID) 
        {
            s_entry_count++;
            //首次找到有效条目
            if (!first_valid_found) 
            {
                s_read_addr = addr;
                first_valid_found = true;
            }
        }
        
        addr += CACHE_ENTRY_SIZE;
    }
    
    if (addr >= CACHE_END_ADDR) 
    {
        s_write_addr = CACHE_START_ADDR;
    }
   
//如果没有找到待传数据，读指针与写指针重合
done:
    if (!first_valid_found) 
    {
        s_read_addr = s_write_addr;
    }
    
    ESP_LOGI(TAG, "init done: pending=%d, write=0x%06lX, overflow=%lu",
                s_entry_count, s_write_addr, s_overflow_count);

    return ERR_OK;
}

/**
 * @brief 查询待补传条数
 * @return 待补传条数（供 M9 LCD 显示"离线缓存 N 条"）
 */
int flash_storage_pending_count(void)
{
    return s_entry_count;
}

/**
 * @brief 取出溢出计数并归零（RAM + NVS 同步清零）
 * @return 自上次调用以来因缓存满被覆盖的条目总数
 * @note cache_task 恢复在线后调用，用于上报 overflow_count 物模型属性
 */
uint32_t flash_storage_take_overflow(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "take_overflow lock timeout");
        return 0;
    }

    uint32_t old = s_overflow_count;
    s_overflow_count = 0;
    overflow_persist();  //NVS清零

    xSemaphoreGive(s_mutex);
    return old;
}

