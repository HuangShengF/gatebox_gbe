#ifndef __BSP_LTR329_H
#define __BSP_LTR329_H

#include "n32l40x.h"
#include "delay.h"
#define LTR329_I2C_ADDR          0x29

// #define LTR329_REG_CONTR         0x80
// #define LTR329_REG_MEAS_RATE     0x85
// #define LTR329_REG_PART_ID       0x86
// #define LTR329_REG_MANUFAC_ID    0x87
// #define LTR329_REG_DATA_CH1_LOW  0x88
// #define LTR329_REG_DATA_CH1_HIGH 0x89
// #define LTR329_REG_DATA_CH0_LOW  0x8A
// #define LTR329_REG_DATA_CH0_HIGH 0x8B
// #define LTR329_REG_STATUS        0x8C

// 寄存器地址定义
#define LTR329_REG_ALS_CONTR     0x80   // ALS控制寄存器
#define LTR329_REG_MEAS_RATE     0x85   // 测量速率寄存器
#define LTR329_REG_PART_ID       0x86   // 部件ID寄存器
#define LTR329_REG_MANUFAC_ID    0x87   // 厂商ID寄存器
#define LTR329_REG_DATA_CH1_0    0x88   // CH1数据低字节
#define LTR329_REG_DATA_CH1_1    0x89   // CH1数据高字节
#define LTR329_REG_DATA_CH0_0    0x8A   // CH0数据低字节
#define LTR329_REG_DATA_CH0_1    0x8B   // CH0数据高字节
#define LTR329_REG_STATUS        0x8C   // 状态寄存器

// 增益配置（对应ALS_CONTR bit4:2，左移2位后的值）
#define LTR329_GAIN_1X           0x00   // 量程 1~64000 lux
#define LTR329_GAIN_2X           0x04   // 量程 0.5~32000 lux
#define LTR329_GAIN_4X           0x08   // 量程 0.25~16000 lux
#define LTR329_GAIN_8X           0x0C   // 量程 0.125~8000 lux
#define LTR329_GAIN_48X          0x18   // 量程 0.02~1300 lux
#define LTR329_GAIN_96X          0x1C   // 量程 0.01~600 lux

// 积分时间配置（对应MEAS_RATE bit5:3，左移3位后的值）
#define LTR329_INT_50MS          0x08
#define LTR329_INT_100MS         0x00   // 默认
#define LTR329_INT_150MS         0x20
#define LTR329_INT_200MS         0x10
#define LTR329_INT_250MS         0x28
#define LTR329_INT_300MS         0x30
#define LTR329_INT_350MS         0x38
#define LTR329_INT_400MS         0x18

// 测量重复速率配置（对应MEAS_RATE bit2:0）
#define LTR329_RATE_50MS         0x00
#define LTR329_RATE_100MS        0x01
#define LTR329_RATE_200MS        0x02
#define LTR329_RATE_500MS        0x03   // 默认
#define LTR329_RATE_1000MS       0x04
#define LTR329_RATE_2000MS       0x05

// 状态寄存器标志位
#define LTR329_STATUS_DATA_VALID 0x80   // 数据有效标志（0=有效，1=无效）
#define LTR329_STATUS_NEW_DATA   0x04   // 新数据标志（1=未读取）

// 函数返回值
#define LTR329_OK                0
#define LTR329_ERR_ID            1      // 器件ID不匹配
#define LTR329_ERR_I2C           2      // I2C通信失败
#define LTR329_ERR_DATA_INVALID  3      // 数据无效

int i2c_master_init(void);
int i2c_master_send(uint8_t* data, int len);
int i2c_master_recv(uint8_t* data, int len);

int LTR329_WriteReg(uint8_t reg, uint8_t value);
int LTR329_ReadRegs(uint8_t reg, uint8_t* data, uint16_t len);
int8_t LTR329_Init(uint8_t gain, uint8_t int_time, uint8_t meas_rate);
uint8_t LTR329_ReadRawData(uint16_t *ch0, uint16_t *ch1);

#endif
