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

    // 示例2: 发送AEHA格式数据（48位）
    // AEHA格式: 通常48位
    uint8_t aeha_data[6] = {0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56};  // 48位数据
    IR_SendData(IR_PROTOCOL_AEHA, aeha_data, 48);
    delay_xms(100);
}

/**
 * @brief NEC协议发送便捷函数
 * @param addr 地址码
 * @param cmd 命令码
 */
void IR_SendNEC(uint8_t addr, uint8_t cmd)
{
    static uint8_t nec_buffer[4];
    nec_buffer[0] = addr;
    nec_buffer[1] = ~addr;
    nec_buffer[2] = cmd;
    nec_buffer[3] = ~cmd;
    IR_SendData(IR_PROTOCOL_NEC, nec_buffer, 32);
}

/**
 * @brief Sony协议发送便捷函数
 * @param cmd 命令码 (7位)
 * @param addr 地址码 (5位)
 */
void IR_SendSony12(uint8_t cmd, uint8_t addr)
{
    static uint8_t sony_buffer[2];
    uint16_t data = ((uint16_t)(cmd & 0x7F) << 5) | (addr & 0x1F);
    sony_buffer[0] = data & 0xFF;
    sony_buffer[1] = (data >> 8) & 0xFF;
    IR_SendData(IR_PROTOCOL_SONY, sony_buffer, 12);
}
