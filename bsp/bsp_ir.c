#include "bsp_ir.h"
#include <stddef.h>
/* 红外发送控制结构 */
typedef struct {
    IR_Protocol_t protocol;
    IR_State_t state;
    const uint8_t *data;      // 字节数组指针
    uint16_t bit_count;       // 总位数
    uint16_t current_bit;     // 当前位索引
    uint8_t is_sending;
} IR_Control_t;

static IR_Control_t ir_ctrl = {
    .protocol = IR_PROTOCOL_NEC,
    .state = IR_STATE_IDLE,
    .data = NULL,
    .bit_count = 0,
    .current_bit = 0,
    .is_sending = 0
};

/* 微秒转定时器计数值 (TIM6: 24MHz / 24 = 1MHz, 1us per tick) */
#define US_TO_TICKS(us) (us)

void IR_PWM_Init(void)
{
    GPIO_InitType GPIO_InitStructure;
    TIM_TimeBaseInitType TIM_TimeBaseStructure;
    OCInitType TIM_OCInitStructure;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO, ENABLE);

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM2, ENABLE);

    GPIO_InitStruct(&GPIO_InitStructure);
    GPIO_InitStructure.Pin            = GPIO_PIN_2;
    GPIO_InitStructure.GPIO_Mode      = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Current   = GPIO_DC_4mA;
    GPIO_InitStructure.GPIO_Alternate = GPIO_AF2_TIM2;
    GPIO_InitStructure.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
    GPIO_InitPeripheral(GPIOA, &GPIO_InitStructure);

    /*
     * TIM2_CLK = 24MHz
     * 24MHz / (631 + 1) = 37.975kHz
     */
    TIM_InitTimBaseStruct(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Prescaler = 0;
    TIM_TimeBaseStructure.Period    = 631;
    TIM_TimeBaseStructure.ClkDiv    = 0;
    TIM_TimeBaseStructure.CntMode   = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM2, &TIM_TimeBaseStructure);

    TIM_InitOcStruct(&TIM_OCInitStructure);
    TIM_OCInitStructure.OcMode      = TIM_OCMODE_PWM1;
    TIM_OCInitStructure.OutputState = TIM_OUTPUT_STATE_DISABLE;
    TIM_OCInitStructure.Pulse       = 211;
    TIM_OCInitStructure.OcPolarity  = TIM_OC_POLARITY_HIGH;
    TIM_InitOc3(TIM2, &TIM_OCInitStructure);

    TIM_ConfigOc3Preload(TIM2, TIM_OC_PRE_LOAD_ENABLE);
    TIM_ConfigArPreload(TIM2, ENABLE);
    TIM_EnableCapCmpCh(TIM2, TIM_CH_3, TIM_CAP_CMP_DISABLE);

    /* 计数器运行，但CH3输出暂时关闭 */
    TIM_Enable(TIM2, ENABLE);
}

void IR_TIM6_Init(void)
{
    TIM_TimeBaseInitType TIM_TimeBaseStructure;
    NVIC_InitType NVIC_InitStructure;
    // 包络定时器: 24MHz / (23+1) = 1MHz, 1us per tick
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM6, ENABLE);
    TIM_InitTimBaseStruct(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Prescaler = 23;       // 24MHz -> 1MHz
    TIM_TimeBaseStructure.Period    = 0xFFFF;   // 将在发送时动态设置
    TIM_TimeBaseStructure.ClkDiv    = 0;
    TIM_TimeBaseStructure.CntMode   = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM6, &TIM_TimeBaseStructure);

    /* Enable the TIM6 global Interrupt */
    NVIC_InitStructure.NVIC_IRQChannel                   = TIM6_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;

    /* 前面的初始化和立即装载可能已经置位更新标志 */
    TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);

    NVIC_Init(&NVIC_InitStructure);
    /* TIM6 enable update irq */
    TIM_ConfigInt(TIM6, TIM_INT_UPDATE, ENABLE);
    /* TIM6 先不启动，发送时再启动 */
    TIM_Enable(TIM6, DISABLE);
}


void IR_Init(void)
{
    // PWM定时器初始化
    IR_PWM_Init();

    // 控制PWM时间的定时器
    IR_TIM6_Init();
}

void IR_Start(void)
{
    // 设置CCR为1/3占空比，PWM输出载波
    TIM2->CCDAT3 = TIM2->AR / 3;
    // 使能CH3输出
    TIM_EnableCapCmpCh(TIM2, TIM_CH_3, TIM_CAP_CMP_ENABLE);
}

void IR_Stop(void)
{
    // CCR=0，PWM输出恒低电平
    TIM2->CCDAT3 = 0;
    // 关闭CH3输出
    TIM_EnableCapCmpCh(TIM2, TIM_CH_3, TIM_CAP_CMP_DISABLE);
}

/* 设置定时器周期并重启 */
static void IR_SetTimerPeriod(uint16_t period_us)
{
    TIM_Enable(TIM6, DISABLE);
    TIM_SetCnt(TIM6, 0);
    TIM_SetAutoReload(TIM6, US_TO_TICKS(period_us) - 1);
    TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
    TIM_Enable(TIM6, ENABLE);
}

/* 发送红外数据 */
void IR_SendData(IR_Protocol_t protocol, const uint8_t *data, uint16_t bits)
{
    if (ir_ctrl.is_sending) {
        return; // 正在发送中
    }

    ir_ctrl.protocol = protocol;
    ir_ctrl.data = data;
    ir_ctrl.bit_count = bits;
    ir_ctrl.current_bit = 0;
    ir_ctrl.state = IR_STATE_START_MARK;
    ir_ctrl.is_sending = 1;

    // 问题4修复：根据协议切换载波频率
    if (protocol == IR_PROTOCOL_SONY) {
        // Sony: 40kHz, 24MHz / 600 = 40kHz
        TIM2->AR = 599;
        TIM2->CCDAT3 = 200;  // 1/3 占空比
    } else {
        // NEC/AEHA: 38kHz, 24MHz / 632 = 37.97kHz
        TIM2->AR = 631;
        TIM2->CCDAT3 = 211;
    }

    uint16_t start_mark_time = 0;
    switch (protocol) {
        case IR_PROTOCOL_NEC:
            start_mark_time = NEC_START_MARK;
            break;
        case IR_PROTOCOL_AEHA:
            start_mark_time = AEHA_START_MARK;
            break;
        case IR_PROTOCOL_SONY:
            start_mark_time = SONY_START_MARK;
            break;
    }

    // 设置TIM6定时器的ARR值，到达这个值会触发中断，也就是header发射完成触发中断
    IR_SetTimerPeriod(start_mark_time);
    // 开始发送起始码的Mark部分
    IR_Start();
}

/* TIM6中断处理 - 状态机 */
void TIM6_IRQHandler(void)
{
    uint16_t space_time, mark_time;  // 变量声明提到switch外

    if (TIM_GetIntStatus(TIM6, TIM_INT_UPDATE) != RESET)
    {
        // 清除中断标志
        TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);

        switch (ir_ctrl.state)
        {
            case IR_STATE_START_MARK:
                // 起始码Mark结束，关闭PWM，进入Space
                IR_Stop();
                ir_ctrl.state = IR_STATE_START_SPACE;

                // 问题3修复：所有协议都要发起始Space
                if (ir_ctrl.protocol == IR_PROTOCOL_SONY) {
                    space_time = SONY_BIT_SPACE;  // Sony: 600µs
                } else if (ir_ctrl.protocol == IR_PROTOCOL_NEC) {
                    space_time = NEC_START_SPACE;
                } else {
                    space_time = AEHA_START_SPACE;
                }
                IR_SetTimerPeriod(space_time);
                break;

            case IR_STATE_START_SPACE:
                // 起始码Space结束，进入数据位Mark
                ir_ctrl.state = IR_STATE_DATA_MARK;

                mark_time = (ir_ctrl.protocol == IR_PROTOCOL_NEC) ?
                                      NEC_BIT_MARK : AEHA_BIT_MARK;
                IR_SetTimerPeriod(mark_time);
                IR_Start();
                break;

            case IR_STATE_DATA_MARK:
                // 数据位Mark结束，关闭PWM
                IR_Stop();

                if (ir_ctrl.protocol == IR_PROTOCOL_SONY) {
                    // Sony协议的Space部分
                    ir_ctrl.state = IR_STATE_DATA_SPACE;
                    IR_SetTimerPeriod(SONY_BIT_SPACE);
                } else {
                    // NEC/AEHA协议的Space部分
                    // 问题2修复：LSB first
                    uint8_t byte_idx = ir_ctrl.current_bit / 8;
                    uint8_t bit_idx = ir_ctrl.current_bit & 7;
                    uint8_t bit = (ir_ctrl.data[byte_idx] >> bit_idx) & 0x01;
                    ir_ctrl.state = IR_STATE_DATA_SPACE;

                    uint16_t space_time;
                    if (ir_ctrl.protocol == IR_PROTOCOL_NEC) {
                        space_time = bit ? NEC_BIT1_SPACE : NEC_BIT0_SPACE;
                    } else {
                        space_time = bit ? AEHA_BIT1_SPACE : AEHA_BIT0_SPACE;
                    }
                    IR_SetTimerPeriod(space_time);
                }
                break;

            case IR_STATE_DATA_SPACE:
                // 数据位Space结束
                ir_ctrl.current_bit++;

                if (ir_ctrl.current_bit >= ir_ctrl.bit_count) {
                    // 所有位发送完成，发送停止位
                    ir_ctrl.state = IR_STATE_STOP;

                    uint16_t stop_time;
                    if (ir_ctrl.protocol == IR_PROTOCOL_NEC) {
                        stop_time = NEC_STOP_MARK;
                    } else if (ir_ctrl.protocol == IR_PROTOCOL_AEHA) {
                        stop_time = AEHA_STOP_MARK;
                    } else {
                        // Sony没有停止位，直接结束
                        IR_Stop();
                        TIM_Enable(TIM6, DISABLE);
                        ir_ctrl.is_sending = 0;
                        ir_ctrl.state = IR_STATE_IDLE;
                        return;
                    }
                    IR_SetTimerPeriod(stop_time);
                    IR_Start();
                } else {
                    // 继续发送下一位
                    ir_ctrl.state = IR_STATE_DATA_MARK;

                    if (ir_ctrl.protocol == IR_PROTOCOL_SONY) {
                        // 问题2修复：LSB first
                        uint8_t byte_idx = ir_ctrl.current_bit / 8;
                        uint8_t bit_idx = ir_ctrl.current_bit & 7;
                        uint8_t bit = (ir_ctrl.data[byte_idx] >> bit_idx) & 0x01;
                        IR_SetTimerPeriod(bit ? SONY_BIT1_MARK : SONY_BIT0_MARK);
                    } else {
                        uint16_t mark_time = (ir_ctrl.protocol == IR_PROTOCOL_NEC) ?
                                              NEC_BIT_MARK : AEHA_BIT_MARK;
                        IR_SetTimerPeriod(mark_time);
                    }
                    IR_Start();
                }
                break;

            case IR_STATE_STOP:
                // 停止位发送完成
                IR_Stop();
                TIM_Enable(TIM6, DISABLE);
                ir_ctrl.is_sending = 0;
                ir_ctrl.state = IR_STATE_IDLE;
                break;

            default:
                break;
        }
    }
}
