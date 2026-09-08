#include "bsp_ir.h"
#include <stddef.h>
#include <string.h>
#include "log.h"
/* 红外发送控制结构 */
typedef struct
{
    IR_Protocol_t protocol;
    IR_State_t state;
    const uint8_t *data;  // 字节数组指针
    uint16_t bit_count;   // 总位数
    uint16_t current_bit; // 当前位索引
    uint8_t is_sending;
} IR_Control_t;

static IR_Control_t ir_ctrl = {
    .protocol = IR_PROTOCOL_NEC,
    .state = IR_STATE_IDLE,
    .data = NULL,
    .bit_count = 0,
    .current_bit = 0,
    .is_sending = 0};

/**************接受中断保存raw data 到数组 *********/ 
typedef struct
{
    uint16_t mark;
    uint16_t space;
} IR_data_t;
typedef struct
{
    IR_data_t *data;
    uint16_t count;
    uint16_t complete_count; /* 帧结束时的对数, 主循环解码用 */
    IR_Protocol_t protocol;
    volatile bool capture_complete;
} IR_RxFrame_t;

/**********解码******************** */
typedef struct
{
    IR_Protocol_t protocol;
    uint16_t bit_count;
    uint8_t data[200];
    uint16_t repeat_count;
} IR_Decoded_t;

IR_Decoded_t ir_decoded = {
    .protocol = IR_PROTOCOL_UNKNOWN,
    .bit_count = 0,
    .data = {0},
    .repeat_count = 0
};
/*********保存上一帧用于比较是不是重复帧************/
static IR_Decoded_t ir_last_frame = {
    .protocol = IR_PROTOCOL_UNKNOWN,
    .bit_count = 0,
    .data = {0},
    .repeat_count = 0
};

// 超时标志：中断检测到130ms超时时置位，主循环清空last_frame后清零
static volatile bool ir_sequence_timeout = false;
static bool g_nec_repeat_frame = false; // NEC重复帧标志

// 用于判断最短帧
#define IR_MIN_PAIRS 12
#define IR_MAX_EDGES 1281   // 这是支持一帧最多多少个bit；；AEHA最长可达1280
// 乒乓缓冲区：创建两个缓冲区A和B
static IR_data_t ir_buffer_A[IR_MAX_EDGES];
static IR_data_t ir_buffer_B[IR_MAX_EDGES];

// 缓冲区A
static IR_RxFrame_t ir_cap_A = {
    .data = ir_buffer_A,
    .count = 0,
    .complete_count = 0,
    .protocol = IR_PROTOCOL_ERROR,
    .capture_complete = false};

// 缓冲区B
static IR_RxFrame_t ir_cap_B = {
    .data = ir_buffer_B,
    .count = 0,
    .complete_count = 0,
    .protocol = IR_PROTOCOL_ERROR,
    .capture_complete = false};

// 当前写入缓冲区指针（中断使用）
static IR_RxFrame_t *ir_cap_write = &ir_cap_A;

// 当前读取缓冲区指针（主循环使用）
static IR_RxFrame_t * volatile ir_cap_read = &ir_cap_B;

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
    GPIO_InitStructure.Pin = GPIO_PIN_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Current = GPIO_DC_12mA;
    GPIO_InitStructure.GPIO_Alternate = GPIO_AF2_TIM2;
    GPIO_InitStructure.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
    GPIO_InitPeripheral(GPIOA, &GPIO_InitStructure);

    /*
     * TIM2_CLK = 24MHz
     * 24MHz / (631 + 1) = 37.975kHz
     */
    TIM_InitTimBaseStruct(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Prescaler = 0;
    TIM_TimeBaseStructure.Period = 631;
    TIM_TimeBaseStructure.ClkDiv = 0;
    TIM_TimeBaseStructure.CntMode = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM2, &TIM_TimeBaseStructure);

    TIM_InitOcStruct(&TIM_OCInitStructure);
    TIM_OCInitStructure.OcMode = TIM_OCMODE_PWM1;
    TIM_OCInitStructure.OutputState = TIM_OUTPUT_STATE_DISABLE;
    TIM_OCInitStructure.Pulse = (631 * 1) / 3;
    TIM_OCInitStructure.OcPolarity = TIM_OC_POLARITY_HIGH;
    TIM_InitOc3(TIM2, &TIM_OCInitStructure);

    TIM_ConfigOc3Preload(TIM2, TIM_OC_PRE_LOAD_ENABLE);
    TIM_ConfigArPreload(TIM2, ENABLE);
    TIM_EnableCapCmpCh(TIM2, TIM_CH_3, TIM_CAP_CMP_DISABLE);

    /* 计数器运行，但CH3输出暂时关闭 */
    TIM_Enable(TIM2, ENABLE);
}

void IR_Capture_Init(void)
{
    GPIO_InitType GPIO_InitStructure;
    TIM_TimeBaseInitType TIM_TimeBaseStructure;
    TIM_ICInitType TIM_ICInitStructure;
    NVIC_InitType NVIC_InitStructure;
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO, ENABLE);

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM5, ENABLE);

    GPIO_InitStruct(&GPIO_InitStructure);
    GPIO_InitStructure.Pin = GPIO_PIN_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Input;
    GPIO_InitStructure.GPIO_Current = GPIO_DC_4mA;
    GPIO_InitStructure.GPIO_Alternate = GPIO_AF1_TIM5;
    GPIO_InitStructure.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
    GPIO_InitPeripheral(GPIOA, &GPIO_InitStructure);

    TIM_InitTimBaseStruct(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Prescaler = 23; // 1个数是1us
    TIM_TimeBaseStructure.Period = 0xFFFF;
    TIM_TimeBaseStructure.ClkDiv = 0;
    TIM_TimeBaseStructure.CntMode = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM5, &TIM_TimeBaseStructure);
    TIM_SetCnt(TIM5, 0);

    TIM_InitIcStruct(&TIM_ICInitStructure);
    TIM_ICInitStructure.Channel = TIM_CH_2;
    TIM_ICInitStructure.IcPolarity = TIM_IC_POLARITY_RISING;
    TIM_ICInitStructure.IcSelection = TIM_IC_SELECTION_INDIRECTTI;
    TIM_ICInitStructure.IcPrescaler = TIM_IC_PSC_DIV1;
    TIM_ICInitStructure.IcFilter = 0x0;
    TIM_ICInit(TIM5, &TIM_ICInitStructure);

    TIM_ICInitStructure.Channel = TIM_CH_1;
    TIM_ICInitStructure.IcPolarity = TIM_IC_POLARITY_FALLING;
    TIM_ICInitStructure.IcSelection = TIM_IC_SELECTION_DIRECTTI;
    TIM_ICInit(TIM5, &TIM_ICInitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = TIM5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* Enable the CC2 Interrupt Request */
    TIM_ConfigInt(TIM5, TIM_INT_CC2 | TIM_INT_CC1 | TIM_INT_UPDATE, ENABLE);
    /* 前面的初始化和立即装载可能已经置位更新标志 */
    TIM_ClrIntPendingBit(TIM5, TIM_INT_UPDATE | TIM_INT_CC1 | TIM_INT_CC2);

    /* TIM enable counter */
    TIM_Enable(TIM5, ENABLE);
}

void IR_TIM6_Init(void)
{
    TIM_TimeBaseInitType TIM_TimeBaseStructure;
    NVIC_InitType NVIC_InitStructure;
    // 包络定时器: 24MHz / (23+1) = 1MHz, 1us per tick
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM6, ENABLE);
    TIM_InitTimBaseStruct(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Prescaler = 23;  // 24MHz -> 1MHz
    TIM_TimeBaseStructure.Period = 0xFFFF; // 将在发送时动态设置
    TIM_TimeBaseStructure.ClkDiv = 0;
    TIM_TimeBaseStructure.CntMode = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM6, &TIM_TimeBaseStructure);

    /* Enable the TIM6 global Interrupt */
    NVIC_InitStructure.NVIC_IRQChannel = TIM6_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;

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

    // 输入捕获定时器初始化
    IR_Capture_Init();
}

void IR_Start(void)
{
    // 设置CCR为1/2占空比，提高Mark期间的平均发射功率
    TIM2->CCDAT3 = (TIM2->AR + 1U) / 3; 
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
    if (ir_ctrl.is_sending)
    {
        return; // 正在发送中
    }

    ir_ctrl.protocol = protocol;
    ir_ctrl.data = data;
    ir_ctrl.bit_count = bits;
    ir_ctrl.current_bit = 0;
    ir_ctrl.state = IR_STATE_START_MARK;
    ir_ctrl.is_sending = 1;

    // 问题4修复：根据协议切换载波频率
    if (protocol == IR_PROTOCOL_SONY)
    {
        // Sony: 40kHz, 24MHz / 600 = 40kHz
        TIM2->AR = 600;
        //TIM2->CCDAT3 = 300; // 1/2 占空比
        // TIM2->CCDAT3 = 200; // 1/2 占空比
        TIM2->CCDAT3 = (TIM2->AR + 1U)  / 3;
    }
    else
    {
        // NEC/AEHA: 38kHz, 24MHz / 632 = 37.97kHz
        TIM2->AR = 631;
        // TIM2->CCDAT3 = 211;
        TIM2->CCDAT3 = (TIM2->AR + 1U)  / 3;
    }

    uint16_t start_mark_time = 0;
    switch (protocol)
    {
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

#define IR_TIMING_TOLERANCE_PERCENT 35U

/* 误差是否落在设定范围内 */
static uint8_t IR_IsNear(uint16_t val, uint16_t center)
{
    uint32_t lower = (uint32_t)center * (100U - IR_TIMING_TOLERANCE_PERCENT) / 100U;
    uint32_t upper = (uint32_t)center * (100U + IR_TIMING_TOLERANCE_PERCENT) / 100U;

    return ((uint32_t)val >= lower) && ((uint32_t)val <= upper);
}

// 判断是否为引导头
static bool IsLeaderMark(uint16_t mark)
{
    return (IR_IsNear(mark, NEC_START_MARK) || IR_IsNear(mark, AEHA_START_MARK) || IR_IsNear(mark, SONY_START_MARK));
}

static void IR_ProcessDecodedFrame(void)
{
    uint16_t byte_count;
    // 处理NEC重复帧
    if(g_nec_repeat_frame)
    {
        // 如果是重复帧，++
        if((ir_last_frame.protocol == IR_PROTOCOL_NEC) && (ir_last_frame.bit_count == 32)) 
        {
            if(ir_last_frame.repeat_count < 255)
            {
                ir_last_frame.repeat_count++;
            }
        }
        return;
    }

    // 如果协议不对，则返回
    if ((ir_decoded.protocol == IR_PROTOCOL_UNKNOWN) ||
        (ir_decoded.protocol == IR_PROTOCOL_ERROR) ||
        (ir_decoded.bit_count == 0U))
    {
        return;
    }

    // 如果是第一帧，把ir_decoded的东西拷贝到ir_last
    if(ir_last_frame.protocol == IR_PROTOCOL_UNKNOWN)
    {
        memcpy(&ir_last_frame, &ir_decoded, sizeof(ir_last_frame));
        ir_last_frame.repeat_count = 1;
        return;
    }

    // AEHA和SONY通过完整重复帧码来判断重复
    byte_count = (ir_decoded.bit_count + 7U) / 8U;
     if ((ir_last_frame.protocol == ir_decoded.protocol) &&
        (ir_last_frame.bit_count == ir_decoded.bit_count) &&
            (memcmp(ir_last_frame.data,ir_decoded.data,byte_count) == 0))
    {
        if(ir_last_frame.repeat_count < 255)
        {
            ir_last_frame.repeat_count++; // 重复帧数加1
        }
    }

    // // 防止有些遥控器发送完整帧
    // if ((ir_last_frame.protocol == IR_PROTOCOL_NEC) &&(memcmp(ir_last_frame.data,ir_decoded.data,4U) == 0))
    // {
    //     if (ir_last_frame.repeat_count < 255U)
    //     {
    //         ir_last_frame.repeat_count++;
    //     }
    //     return;
    // }
}

static void IR_FinishEvent(void)
{
    uint16_t byte_count;

    if (ir_last_frame.protocol == IR_PROTOCOL_UNKNOWN)
    {
        return;
    }
    byte_count = (ir_last_frame.bit_count + 7U) / 8U;

    printf("IR proto=%u bits=%u repeat=%u data:",
           ir_last_frame.protocol,
           ir_last_frame.bit_count,
           ir_last_frame.repeat_count);

    for (uint16_t i = 0; i < byte_count; i++)
    {
        printf(" %02X", ir_last_frame.data[i]);
    }

    printf("\r\n");

    /* 后续根据protocol生成0x2401通知 */

    memset(&ir_last_frame, 0, sizeof(ir_last_frame));
    ir_last_frame.protocol = IR_PROTOCOL_UNKNOWN;
}

// AEHA 最多支持1280bit
static IR_DecodeErr_t IR_DecodeFrame(const IR_RxFrame_t *frame)
{
    uint16_t nbits = 0;

    // 使用主循环已领取的固定缓冲区
    uint16_t count = frame->complete_count;

     g_nec_repeat_frame = false;
    // 先检测 NEC repeat 帧（只有1对数据）
    if (count == 1)
    {
        // NEC repeat: 9000us mark + 2250us space + 560us 尾mark
        if (IR_IsNear(frame->data[0].mark, 9000) &&
            IR_IsNear(frame->data[0].space, 2250) && IR_IsNear(frame->data[1].mark,NEC_STOP_MARK))
        {
            // 这是 NEC repeat，直接返回成功，不修改 ir_decoded
            // ir_decoded 保持上一帧的数据不变
              g_nec_repeat_frame = true;
            return IR_DECODE_OK;
        }
        return IR_DECODE_ERR_TOO_SHORT;
    }

    if (count < IR_MIN_PAIRS)
    {
        return IR_DECODE_ERR_TOO_SHORT; // 数据不够，最低是 sony的12bit
    }

    memset(&ir_decoded, 0, sizeof(ir_decoded));
    ir_decoded.protocol = IR_PROTOCOL_UNKNOWN;


    // 把接受到的数据解码
    /* ---- 1. 识别协议: mark 和 space 都要落在窗口内 ---- */
    if (IR_IsNear(frame->data[0].mark, NEC_START_MARK) &&
        IR_IsNear(frame->data[0].space, NEC_START_SPACE))
    {
        // 标准单帧NEC
        if(count != 33)
        {
            return IR_DECODE_ERR_NEC_LEN;
        }
        ir_decoded.protocol = IR_PROTOCOL_NEC;
    }
    else if (IR_IsNear(frame->data[0].mark, AEHA_START_MARK) &&
             IR_IsNear(frame->data[0].space, AEHA_START_SPACE))
    {
        ir_decoded.protocol = IR_PROTOCOL_AEHA;
    }
    else if (IR_IsNear(frame->data[0].mark, SONY_START_MARK) &&
             IR_IsNear(frame->data[0].space, SONY_BIT_SPACE))
    {
        ir_decoded.protocol = IR_PROTOCOL_SONY;
    }
    else
    {
       ir_decoded.protocol = IR_PROTOCOL_UNKNOWN;
    }

    switch (ir_decoded.protocol)
    {
        case IR_PROTOCOL_NEC:
        {
            if (count < 33)
            {
                return IR_DECODE_ERR_NEC_LEN;
            }
            nbits = 32;
            for (uint16_t i = 0; i < nbits; i++)
            {
                /* space 1690=1 / 560=0, 阈值 1100 */
                // 这个数组里面存的是字节
                ir_decoded.data[i / 8] |=
                    (uint8_t)((frame->data[1 + i].space > 1100) ? 1 : 0) << (i % 8);
            }
            break;
        }

        case IR_PROTOCOL_AEHA:
        {
            uint16_t end = count; /* 无重发: 停止位是最后一个 mark */
            for (uint16_t i = 1; i < count; i++)
            {
                if (frame->data[i].space >= 4000)
                {
                    end = i;
                    break;
                }
            }
            nbits = end - 1;
            if (nbits < 48)
                return IR_DECODE_ERR_AEHA_LEN; /* AEHA 最短 48 位 */
            for (uint16_t i = 0; i < nbits; i++)
            {
                /* space 1275=1 / 425=0, 阈值 850 */
                ir_decoded.data[i / 8] |=
                    (uint8_t)((frame->data[1 + i].space > 850) ? 1 : 0) << (i % 8);
            }
            break;
        }


        case IR_PROTOCOL_SONY:
        {
            /* Sony 无停止位: 单发时最后一位 mark 在 data[count], 位数 = count;
            * 重发时最后一位 space 被帧间隙撑大(≥5ms), 用它定位 */
            nbits = count;
            for (uint16_t i = 1; i < count; i++)
            {
                if (frame->data[i].space >= 5000)
                {
                    nbits = i;
                    break;
                }
            }
            if (nbits != 12 && nbits != 15 && nbits != 20)
                return IR_DECODE_ERR_SONY_LEN;
            for (uint16_t i = 0; i < nbits; i++)
            {
                /* mark 1200=1 / 600=0 */
                ir_decoded.data[i / 8] |=
                    (uint8_t)((frame->data[1 + i].mark >= 900) ? 1 : 0) << (i % 8);
            }
            break;
        }

        case IR_PROTOCOL_UNKNOWN:
        {
            // 不解码，
            return IR_DECODE_ERR_UNKNOWN_PROTO;
        }

    }
    ir_decoded.bit_count = nbits;
    return IR_DECODE_OK;
}

/* 主循环轮询: 学到一帧就打印 */
void IR_Poll(void)
{
    IR_DecodeErr_t err;
    IR_RxFrame_t *frame = NULL;
    bool sequence_timeout = false;

    /* 原子领取完整帧和超时标志，避免最后一帧被拆成新事件 */
    NVIC_DisableIRQ(TIM5_IRQn);
    if (ir_cap_read->capture_complete)
    {
        frame = ir_cap_read;
        frame->capture_complete = false;
    }
    if (ir_sequence_timeout)
    {
        ir_sequence_timeout = false;
        sequence_timeout = true;
    }
    NVIC_EnableIRQ(TIM5_IRQn);

    /* 先处理最后收到的物理帧 */
    if (frame != NULL)
    {
        err = IR_DecodeFrame(frame);

        if (err == IR_DECODE_OK)
        {
            IR_ProcessDecodedFrame();
        }
        else
        {
            printf("IR decode err=%d\r\n", err);
        }
    }

    // 处理完最后一帧后，再结束本次按键序列
    if (sequence_timeout)
    {
        IR_FinishEvent();
    }

    // if (!ir_cap_read->capture_complete) return;
    // ir_cap_read->capture_complete = false;
    // if (ir_ctrl.is_sending) return;      /* 自己发射的回声不学习 */

    // err = IR_DecodeFrame();
    // // IR_Poll 里, err 打印下面加:
    // printf("  count=%u:", ir_cap_read->complete_count);
    // for (uint16_t i = 0; i < ir_cap_read->complete_count; i++) {
    //     printf(" %u/%u", ir_cap_read->data[i].mark, ir_cap_read->data[i].space);
    // }
    // printf("\r\n");
    // printf("  last pair [%u]: mark=%u space=%u\r\n", ir_cap_read->complete_count,
    //        ir_cap_read->data[ir_cap_read->complete_count - 1].mark,
    //        ir_cap_read->data[ir_cap_read->complete_count - 1].space);

    // if (err != IR_DECODE_OK)
    // {
    //     printf("IR decode err=%d\r\n", err);
    //     /* 临时调试: 看原始引导码 */
    //     printf("  raw[0] mark=%u space=%u count=%u\r\n",
    //            ir_cap_read->data[0].mark, ir_cap_read->data[0].space, ir_cap_read->complete_count);
    //     return;
    // }

    // // 解码成功
    // // 判断是否是重复帧
    // bool is_repeat = false;
    // if (ir_last_frame.protocol == ir_decoded.protocol && ir_last_frame.bit_count == ir_decoded.bit_count)
    // {
    //     // 向上取整
    //      uint16_t byte_count = (ir_decoded.bit_count + 7) / 8;
    //     if (memcmp(ir_last_frame.data, ir_decoded.data, byte_count) == 0)
    //     {
    //         is_repeat = true;
    //     }
    // }
    // if (is_repeat)
    // {
    //     // 重复帧：只增加计数
    //     ir_decoded.repeat_count++;
    //     printf("repeat=%u\r\n", ir_decoded.repeat_count);
    // }
    // else
    // {
    //     // 新帧：重置计数并打印
    //     ir_decoded.repeat_count = 0;

    //     printf("IR proto=%d bits=%d data:", ir_decoded.protocol, ir_decoded.bit_count);
    //     for (uint16_t i = 0; i < (ir_decoded.bit_count + 7) / 8; i++)
    //     {
    //         printf(" %02X", ir_decoded.data[i]);
    //     }
    //     printf("\r\n");

    //     /* 调试: Sony 单发时显示最后一位的 mark */
    //     if (ir_decoded.protocol == IR_PROTOCOL_SONY) {
    //         printf("  tail mark=%u\r\n", ir_cap_read->data[ir_cap_read->complete_count].mark);
    //     }

    //     // 保存当前帧用于下次比对
    //     memcpy(&ir_last_frame, &ir_decoded, sizeof(IR_Decoded_t));
    // }

    // // 清空解码缓冲区
    // memset(ir_decoded.data, 0, sizeof(ir_decoded.data));
    // ir_decoded.bit_count = 0;
}


// 输入捕获中断
void TIM5_IRQHandler(void)
{
    static uint8_t started = 0;
    static uint8_t timeout_cnt = 0;  // 超时计数器

    if (TIM_GetIntStatus(TIM5, TIM_INT_UPDATE) != RESET)
    {
        // 更新中断 (65ms溢出)
        TIM_ClrIntPendingBit(TIM5, TIM_INT_UPDATE);

        if (started)
        {
            if (++timeout_cnt >= 2U)  // 溢出2次，约130ms
            {
                if (ir_cap_write->count >= 1U) // 这里count必须>=1，否则会丢NEC repeat
                {
                    // 如果超时了，认为已经接收完成，这时候需要交换缓冲区
                    ir_cap_write->complete_count = ir_cap_write->count;
                    ir_cap_write->capture_complete = true;

                    // 切换乒乓缓冲区
                    IR_RxFrame_t *temp = ir_cap_write;
                    ir_cap_write = ir_cap_read;
                    ir_cap_read = temp;

                    // 重置新的写缓冲区
                    ir_cap_write->count = 0;
                    ir_cap_write->capture_complete = false;
                }

                // 超时，标记按键序列结束
                ir_sequence_timeout = true;

                started = 0;
                timeout_cnt = 0;
            }
        }
    }
    if (TIM_GetIntStatus(TIM5, TIM_INT_CC1) != RESET)
    {
        // 下降沿
        TIM_ClrIntPendingBit(TIM5, TIM_INT_CC1);

        if (!started)
        {
            started = 1;
            TIM_SetCnt(TIM5, 0);
            timeout_cnt = 0;  // 新帧开始,重置超时计数
            return;
        }
        ir_cap_write->data[ir_cap_write->count].space = TIM_GetCap1(TIM5);
        TIM_SetCnt(TIM5, 0);
        timeout_cnt = 0;
        if (ir_cap_write->count < IR_MAX_EDGES - 1)
        {
            ir_cap_write->count++;
        }
    }
    if (TIM_GetIntStatus(TIM5, TIM_INT_CC2) != RESET)
    {
        // 上升沿
        TIM_ClrIntPendingBit(TIM5, TIM_INT_CC2);
        if (started)
        {
            // 防止空闲时有毛刺
            uint16_t mark_time = TIM_GetCap2(TIM5);
            ir_cap_write->data[ir_cap_write->count].mark = mark_time;

            if(ir_cap_write->count > 0)
            {
               // 至少不是第一个
                // 获取上一个的space时间
                uint16_t last_space = ir_cap_write->data[ir_cap_write->count - 1].space;
                //这里ir_cap_write->count必须>=1(不然会丢NEC的repeat)
                if(IsLeaderMark(mark_time) && (last_space >= 3000U) && ir_cap_write->count >= 1)
                {
                    // 说明是重复帧,退出来解码，但是下一帧的mark已经捕获我们不能丢弃，存下来
                    ir_cap_write->complete_count = ir_cap_write->count - 1;  /* 先把帧长存下来再清零 */
                    ir_cap_write->capture_complete = true;
                    // 切换乒乓缓冲区
                    IR_RxFrame_t *temp = ir_cap_write;
                    ir_cap_write = ir_cap_read;
                    ir_cap_read = temp;
                    
                    // 新缓冲区开始接收新帧
                    ir_cap_write->count = 0;
                    ir_cap_write->capture_complete = false;
                    ir_cap_write->data[0].mark = mark_time;
                    
                    TIM_SetCnt(TIM5, 0);
                    timeout_cnt = 0;
                    return;
                }

            }
        }

        TIM_SetCnt(TIM5, 0);
        timeout_cnt = 0;  // 收到边沿,重置超时计数
    }
}

/* TIM6中断处理 - 状态机 */
// 发射状态机
void TIM6_IRQHandler(void)
{
    uint16_t space_time, mark_time; // 变量声明提到switch外

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

            if (ir_ctrl.protocol == IR_PROTOCOL_SONY)
            {
                space_time = SONY_BIT_SPACE; // Sony: 600µs
            }
            else if (ir_ctrl.protocol == IR_PROTOCOL_NEC)
            {
                space_time = NEC_START_SPACE;
            }
            else
            {
                space_time = AEHA_START_SPACE;
            }
            IR_SetTimerPeriod(space_time);
            break;

        case IR_STATE_START_SPACE:
            // 起始码Space结束，进入数据位Mark
            ir_ctrl.state = IR_STATE_DATA_MARK;

            // sony跟1和0区别在于PWM持续时间；NEC和AEHA是space空闲低电平的时间区别1和0
            if (ir_ctrl.protocol == IR_PROTOCOL_SONY)
            {
                uint8_t byte_idx = ir_ctrl.current_bit / 8;
                uint8_t bit_idx = ir_ctrl.current_bit & 7;
                uint8_t bit = (ir_ctrl.data[byte_idx] >> bit_idx) & 0x01;
                mark_time = (bit == 0) ? SONY_BIT0_MARK : SONY_BIT1_MARK;
            }
            else
            {
                mark_time = (ir_ctrl.protocol == IR_PROTOCOL_NEC) ? NEC_BIT_MARK : AEHA_BIT_MARK;
            }

            IR_SetTimerPeriod(mark_time);
            IR_Start();
            break;

        case IR_STATE_DATA_MARK:
            // 数据位Mark结束，关闭PWM
            IR_Stop();

            if (ir_ctrl.protocol == IR_PROTOCOL_SONY)
            {
                // Sony协议的Space部分
                ir_ctrl.state = IR_STATE_DATA_SPACE;
                IR_SetTimerPeriod(SONY_BIT_SPACE);
            }
            else
            {
                // NEC/AEHA协议的Space部分
                uint8_t byte_idx = ir_ctrl.current_bit / 8;
                uint8_t bit_idx = ir_ctrl.current_bit & 7;
                uint8_t bit = (ir_ctrl.data[byte_idx] >> bit_idx) & 0x01;
                ir_ctrl.state = IR_STATE_DATA_SPACE;

                uint16_t space_time;
                if (ir_ctrl.protocol == IR_PROTOCOL_NEC)
                {
                    space_time = bit ? NEC_BIT1_SPACE : NEC_BIT0_SPACE;
                }
                else
                {
                    space_time = bit ? AEHA_BIT1_SPACE : AEHA_BIT0_SPACE;
                }
                IR_SetTimerPeriod(space_time);
            }
            break;

        case IR_STATE_DATA_SPACE:
            // 数据位Space结束
            ir_ctrl.current_bit++;
            if (ir_ctrl.current_bit >= ir_ctrl.bit_count)
            {
                // 所有位发送完成，发送停止位
                ir_ctrl.state = IR_STATE_STOP;

                uint16_t stop_time;
                if (ir_ctrl.protocol == IR_PROTOCOL_NEC)
                {
                    stop_time = NEC_STOP_MARK;
                }
                else if (ir_ctrl.protocol == IR_PROTOCOL_AEHA)
                {
                    stop_time = AEHA_STOP_MARK;
                }
                else
                {
                    // Sony没有停止位，直接结束
                    IR_Stop();
                    TIM_Enable(TIM6, DISABLE);
                    ir_ctrl.is_sending = 0;
                    ir_ctrl.state = IR_STATE_IDLE;
                    return;
                }
                IR_SetTimerPeriod(stop_time);
                IR_Start();
            }
            else
            {
                // 继续发送下一位
                ir_ctrl.state = IR_STATE_DATA_MARK;

                if (ir_ctrl.protocol == IR_PROTOCOL_SONY)
                {
                    // 问题2修复：LSB first
                    uint8_t byte_idx = ir_ctrl.current_bit / 8;
                    uint8_t bit_idx = ir_ctrl.current_bit & 7;
                    uint8_t bit = (ir_ctrl.data[byte_idx] >> bit_idx) & 0x01;
                    IR_SetTimerPeriod(bit ? SONY_BIT1_MARK : SONY_BIT0_MARK);
                }
                else
                {
                    uint16_t mark_time = (ir_ctrl.protocol == IR_PROTOCOL_NEC) ? NEC_BIT_MARK : AEHA_BIT_MARK;
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
