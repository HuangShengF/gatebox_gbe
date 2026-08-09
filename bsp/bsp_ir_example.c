/**
 * @file bsp_ir_example.c
 * @brief 红外发射使用示例
 */

#include "bsp_ir.h"
#include "delay.h"

/**
 * @brief 红外发射使用示例
 */
void IR_Example(void)
{
    // 初始化红外模块
    IR_Init();

    // 示例1: 发送NEC格式数据
    // NEC格式: 32位 (地址8位 + 地址反码8位 + 命令8位 + 命令反码8位)
    uint8_t nec_addr = 0x00;
    uint8_t nec_cmd = 0x45;  // 例如: 电源键
    uint32_t nec_data = ((uint32_t)nec_addr << 24) |
                        ((uint32_t)(~nec_addr) << 16) |
                        ((uint32_t)nec_cmd << 8) |
                        ((uint32_t)(~nec_cmd));

    IR_SendData(IR_PROTOCOL_NEC, nec_data, 32);
    delay_ms(100);  // 等待发送完成

    // 示例2: 发送AEHA格式数据
    // AEHA格式: 通常48位
    uint64_t aeha_data = 0x123456789ABC;
    IR_SendData(IR_PROTOCOL_AEHA, (uint32_t)(aeha_data & 0xFFFFFFFF), 32);
    delay_ms(100);

    // 示例3: 发送Sony格式数据
    // Sony格式: 12位或15位或20位
    // 12位格式: 命令7位 + 地址5位
    uint8_t sony_cmd = 0x15;   // 7位命令
    uint8_t sony_addr = 0x01;  // 5位地址
    uint16_t sony_data = ((uint16_t)sony_cmd << 5) | sony_addr;

    IR_SendData(IR_PROTOCOL_SONY, sony_data, 12);
    delay_ms(100);
}

/**
 * @brief NEC协议发送便捷函数
 * @param addr 地址码
 * @param cmd 命令码
 */
void IR_SendNEC(uint8_t addr, uint8_t cmd)
{
    uint32_t data = ((uint32_t)addr << 24) |
                    ((uint32_t)(~addr) << 16) |
                    ((uint32_t)cmd << 8) |
                    ((uint32_t)(~cmd));
    IR_SendData(IR_PROTOCOL_NEC, data, 32);
}

/**
 * @brief Sony协议发送便捷函数
 * @param cmd 命令码 (7位)
 * @param addr 地址码 (5位)
 */
void IR_SendSony12(uint8_t cmd, uint8_t addr)
{
    uint16_t data = ((uint16_t)(cmd & 0x7F) << 5) | (addr & 0x1F);
    IR_SendData(IR_PROTOCOL_SONY, data, 12);
}
