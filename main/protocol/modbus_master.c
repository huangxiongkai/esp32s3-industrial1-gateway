#include "modbus_master.h"
#include "modbus_crc.h"
#include "drivers/rs485.h"
#include "esp_log.h"
#include <string.h>
#include "types.h"
 
static const char *TAG = "MODBUS";      


// ---------- 内部：发送 0x03 请求帧 ----------
/**
 * @brief 组装并发送 0x03 读保持寄存器请求帧
 *
 * 帧结构（固定 8 字节）：
 * [从站地址][0x03][寄存器高][寄存器低][数量高][数量低][CRC低][CRC高]
*
* @param addr  从站地址
* @param reg   起始寄存器地址
* @param count 读取寄存器数量
*/
static void modbus_send_request(uint8_t addr, uint16_t reg, uint16_t count)
{
    //组帧
    uint8_t frame[8];
    frame[0] = addr;
    frame[1] = 0x03;
    frame[2] = reg >> 8;        //【左移8位取高字节】
    frame[3] = reg & 0xFF;      //【取低字节】
    frame[4] = count >> 8;
    frame[5] = count & 0xFF;

    uint16_t crc = modbus_crc16(frame, 6);
    frame[6] = crc & 0xFF;        // CRC 低字节先发
    frame[7] = crc >> 8;          // CRC 高字节后发

    rs485_flush();                // 清空残留


    //发送
    rs485_send(frame, 8);
}


/**
 * @brief 接收从站响应并做 CRC 整帧校验
 *
 * 校验方式：对整帧（含末尾 2 字节 CRC）算 modbus_crc16，
 * 结果为 0 表示数据完整（利用 CRC 数学性质）。
 *
 * @param buf 接收缓冲区（调用者分配）
 * @param len [out] 实际接收到的字节数
 * @return error_t ERR_OK=校验通过, ERR_TIMEOUT=无响应, ERR_CRC=校验失败
*/
static error_t modbus_recv_response(uint8_t *buf, int *len)
{
    int n = rs485_receive(buf, MODBUS_RESP_BUF, MODBUS_TIMEOUT_MS);
    //解决首帧数据异常问题
    int offset = 0;
    while (offset < n && buf[offset] == 0x00)   
    {
        offset ++;
    }
    if (offset > 0)
    {
        ESP_LOGD(TAG, "Skipped %d glitch byte(s)", offset);
            memmove(buf, buf + offset, n - offset);   //把有效帧移到缓冲区头部
            n -= offset;
    }
    

        if (n == 0) 
        {
            return ERR_TIMEOUT;
        }
        if (modbus_crc16(buf, n) != 0)
        {
            ESP_LOGW(TAG, "CRC error, len=%d", n);
            ESP_LOG_BUFFER_HEX(TAG, buf, n);   //打印收到的所有字节
            return ERR_CRC;
        }
        *len = n;
        return ERR_OK;
        

}

/**
 * @brief 从 0x03 正常响应帧中提取寄存器原始值
 *
 * 响应帧布局：[addr][0x03][byte_count][数据区...][CRC低][CRC高]
* 数据区从 resp[3] 开始，每个寄存器占 2 字节，大端序（高字节在前）。
*
* @param resp  完整响应帧
* @param regs  [out] 输出的寄存器原始值数组
* @param count 寄存器数量
*/
static void modbus_parse_registers(const uint8_t *resp, uint16_t *regs,uint16_t count)
{
    const uint8_t *data = &resp[3];
        for (uint16_t i = 0; i < count; i++) 
        {
            regs[i] = (data[i * 2] << 8) | data[i * 2 + 1];
        }
}


//读设备接口
/**
 * @brief 读取一个从站设备的全部寄存器并填入 device_data_t
 *
 * 完整通信流程：
 *   1. 组帧，按 map 表逐条发送 0x03 请求
 *   2. 等待响应，超时返回 ERR_TIMEOUT
 *   3. CRC 整帧校验，失败返回 ERR_CRC
 *   4. 检查异常帧（功能码 bit7=1），返回 ERR_OFFLINE
 *   5. 校验响应长度 = 5 + byte_count
 *   6. 解析寄存器 → 乘缩放因子 → 写入 out 对应字段
 *
 * @param dev_id    设备逻辑 ID
 * @param map       寄存器映射表
 * @param map_count 映射条目数（1=电流设备, 2=温湿度设备）
 * @param out       [out] 填充后的设备数据（online=true, ts 由调用者填）
 * @return error_t  ERR_OK / ERR_TIMEOUT / ERR_CRC / ERR_OFFLINE
*/

error_t modbus_read_device(uint8_t dev_id, const modbus_reg_map_t *map
                              ,int map_count, device_data_t *out)
{
    uint8_t resp[MODBUS_RESP_BUF];
    uint16_t regs[MODBUS_MAX_REG];
    int resp_len = 0;

    //发送前先清空
    memset(out, 0, sizeof(device_data_t));
    out->id = dev_id;

    for (int i = 0; i < map_count; i++)
    {
        //1.组帧发送
        modbus_send_request(map[i].slave_addr , map[i].reg_start , 1);
        
        //2.接收 + 校验
        error_t err = modbus_recv_response(resp , &resp_len);
        if (err != ERR_OK)  
        {
            return err;
        }
        //异常处理
        //【1】.检查异常帧
        if (resp[1] & 0x80)  //检查功能码最高位是否为1
        {
            ESP_LOGW(TAG, "Slave %d exception: 0x%02X",
            map[i].slave_addr, resp[2]);
            return ERR_OFFLINE;
        }
        //【2】.检验校验长度
        uint8_t byte_count = resp[2];
        if (resp_len != (5 + byte_count)) 
        {
            ESP_LOGW(TAG, "Length mismatch: got %d, expect%d",
                resp_len, 5 + byte_count);
            return ERR_CRC;
        }

        //3.解析寄存器
        modbus_parse_registers(resp , regs ,1);

        //4.映射寄存器
        float value = regs[0] * map[i].scale;
 
        //把value存入对应寄存器
        switch (map[i].field) 
        {
        case FIELD_TEMP:    out->temp    = value; break;
        case FIELD_HUMI:    out->humi    = value; break;
        case FIELD_CURRENT: out->current = value; break;
        }
    }

        out->online = true;
        out->ts = 0;  //由poll_scheduler填入实际时间戳
        return ERR_OK;
    
}

