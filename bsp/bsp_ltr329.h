#ifndef __BSP_LTR329_H
#define __BSP_LTR329_H

#include "n32l40x.h"

#define LTR329_I2C_ADDR          0x29

#define LTR329_REG_CONTR         0x80
#define LTR329_REG_MEAS_RATE     0x85
#define LTR329_REG_PART_ID       0x86
#define LTR329_REG_MANUFAC_ID    0x87
#define LTR329_REG_DATA_CH1_LOW  0x88
#define LTR329_REG_DATA_CH1_HIGH 0x89
#define LTR329_REG_DATA_CH0_LOW  0x8A
#define LTR329_REG_DATA_CH0_HIGH 0x8B
#define LTR329_REG_STATUS        0x8C

int i2c_master_init(void);
int i2c_master_send(uint8_t* data, int len);
int i2c_master_recv(uint8_t* data, int len);

#endif
