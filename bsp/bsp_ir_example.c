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

    static const uint8_t sony_data[2] = {0x95, 0x00};

    IR_SendData(IR_PROTOCOL_SONY, sony_data, 12);
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
    uint16_t data = (uint16_t)(cmd & 0x7F) | ((addr & 0x1F) << 7);
    sony_buffer[0] = data & 0xFF;
    sony_buffer[1] = (data >> 8) & 0xFF;
    IR_SendData(IR_PROTOCOL_SONY, sony_buffer, 12);
}
