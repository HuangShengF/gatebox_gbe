#ifndef BSP_IR_H
#define BSP_IR_H

#include "n32l40x.h"

/* 红外协议类型 */
typedef enum {
    IR_PROTOCOL_UNKNOWN = -1,
    IR_PROTOCOL_NEC = 0,
    IR_PROTOCOL_AEHA,
    IR_PROTOCOL_SONY
} IR_Protocol_t;

/* 发送状态 */
typedef enum {
    IR_STATE_IDLE = 0,
    IR_STATE_START_MARK,
    IR_STATE_START_SPACE,
    IR_STATE_DATA_MARK,
    IR_STATE_DATA_SPACE,
    IR_STATE_STOP
} IR_State_t;

/* NEC协议时序 (us) */
#define NEC_START_MARK      9000
#define NEC_START_SPACE     4500
#define NEC_BIT_MARK        560
#define NEC_BIT1_SPACE      1690
#define NEC_BIT0_SPACE      560
#define NEC_STOP_MARK       560

/* AEHA协议时序 (us) */
#define AEHA_START_MARK     3400
#define AEHA_START_SPACE    1700
#define AEHA_BIT_MARK       425
#define AEHA_BIT1_SPACE     1275
#define AEHA_BIT0_SPACE     425
#define AEHA_STOP_MARK      425

/* Sony协议时序 (us) */
#define SONY_START_MARK     2400
#define SONY_BIT1_MARK      1200
#define SONY_BIT0_MARK      600
#define SONY_BIT_SPACE      600

void IR_Init(void);
void IR_Start(void);
void IR_Stop(void);
void IR_SendData(IR_Protocol_t protocol, const uint8_t *data, uint16_t bits);

#endif
