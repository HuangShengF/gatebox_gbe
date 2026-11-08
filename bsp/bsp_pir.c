#include "bsp_pir.h"
#include "usb_pwr.h"

#define PIR_PORT              GPIOA
/* 左右引脚映射需要结合原理图或实机方向确认 */
#define PIR_LEFT_PIN          GPIO_PIN_3
#define PIR_RIGHT_PIN         GPIO_PIN_7
#define PIR_PIN               (PIR_LEFT_PIN | PIR_RIGHT_PIN)
#define PIR_PORT_SOURCE       GPIOA_PORT_SOURCE
#define PIR_WAKEUP_TIMER_HZ   1000U
#define PIR_WAKEUP_TICK_MS    10U

typedef enum
{
    PIR_WAKEUP_DISABLED = 0,
    PIR_WAKEUP_WAIT_LOW,
    PIR_WAKEUP_QUIET,
    PIR_WAKEUP_ARMED
} PIR_WakeupState;


static volatile uint8_t g_pir_changed_flags = 0U;
static volatile uint8_t g_pir_left_snapshot = 0U;
static volatile uint8_t g_pir_right_snapshot = 0U;
static volatile PIR_WakeupState g_pir_wakeup_state = PIR_WAKEUP_DISABLED;
static volatile uint16_t g_pir_quiet_ticks = 0U; // 两路连续 LOW 的计时
static volatile uint16_t g_pir_left_high_ticks = 0U; // 左路连续 HIGH 的计时
static volatile uint16_t g_pir_right_high_ticks = 0U; // 右路连续 HIGH 的计时
/* 仅上电/MCU复位清零；协议会话和USB总线复位不改变配置。 */
static volatile uint8_t g_pir_wakeup_enabled = 0U; // PC 配置的开关，默认 0
static volatile uint8_t g_pir_wakeup_sent = 0U; // 本轮睡眠是否已经请求过唤醒
static uint8_t g_pir_wakeup_initialized = 0U;

static void PIR_WakeupTimerInit(void)
{
    RCC_ClocksType clocks;
    TIM_TimeBaseInitType TIM_TimeBaseStructure;
    NVIC_InitType NVIC_InitStructure;
    uint32_t timer_clock;
    uint32_t prescaler_div;

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM4, ENABLE);

    RCC_GetClocksFreqValue(&clocks);
    if (clocks.Pclk1Freq == clocks.HclkFreq)
    {
        timer_clock = clocks.Pclk1Freq;
    }
    else
    {
        timer_clock = clocks.Pclk1Freq * 2U;
    }
    prescaler_div = timer_clock / PIR_WAKEUP_TIMER_HZ;

    TIM_InitTimBaseStruct(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Prescaler = (uint16_t)(prescaler_div - 1U);
    /* 每10ms采样；只有启用PIR唤醒且USB挂起时才运行。 */
    TIM_TimeBaseStructure.Period = (uint16_t)(PIR_WAKEUP_TICK_MS - 1U);
    TIM_TimeBaseStructure.ClkDiv = 0;
    TIM_TimeBaseStructure.CntMode = TIM_CNT_MODE_UP;
    TIM_InitTimeBase(TIM4, &TIM_TimeBaseStructure);
    TIM_SelectOnePulseMode(TIM4, TIM_OPMODE_REPET);
    TIM_SetCnt(TIM4, 0U);
    TIM_ClrIntPendingBit(TIM4, TIM_INT_UPDATE);
    TIM_ConfigInt(TIM4, TIM_INT_UPDATE, ENABLE);
    TIM_Enable(TIM4, DISABLE);

    NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    g_pir_wakeup_initialized = 1U;
}

void PIR_ExtiInit(void)
{
    GPIO_InitType GPIO_InitStructure;
    EXTI_InitType EXTI_InitStructure;
    NVIC_InitType NVIC_InitStructure;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO, ENABLE);

    GPIO_InitStruct(&GPIO_InitStructure);
    GPIO_InitStructure.Pin       = PIR_PIN;
    GPIO_InitStructure.GPIO_Pull = GPIO_Pull_Down;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Input;
    GPIO_InitPeripheral(PIR_PORT, &GPIO_InitStructure);

    GPIO_ConfigEXTILine(PIR_PORT_SOURCE, GPIO_PIN_SOURCE3);
    GPIO_ConfigEXTILine(PIR_PORT_SOURCE, GPIO_PIN_SOURCE7);

    EXTI_ClrITPendBit(EXTI_LINE3 | EXTI_LINE7);
    EXTI_InitStructure.EXTI_Line    = EXTI_LINE3 | EXTI_LINE7;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling; // 双边沿触发
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = EXTI3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_Init(&NVIC_InitStructure);

    PIR_WakeupTimerInit();

    if (bDeviceState == SUSPENDED)
    {
        PIR_WakeupSuspend();
    }
}

void PIR_GetStates(uint8_t *left_state, uint8_t *right_state)
{
 
    if ((left_state == NULL) || (right_state == NULL))
    {
        return;
    }

    *left_state = GPIO_ReadInputDataBit(PIR_PORT, PIR_LEFT_PIN);
    *right_state = GPIO_ReadInputDataBit(PIR_PORT, PIR_RIGHT_PIN);
}

//记录两路PIR状态的变化，并更新状态机；；边沿触发中断
void PIR_RecordChangeFromISR(uint8_t flags)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    g_pir_left_snapshot =(uint8_t)GPIO_ReadInputDataBit(PIR_PORT, PIR_LEFT_PIN);

    g_pir_right_snapshot =(uint8_t)GPIO_ReadInputDataBit(PIR_PORT, PIR_RIGHT_PIN);

    g_pir_changed_flags |= flags;

    /* 边沿打断连续计时，避免两个采样点之间的短脉冲被漏算。 */
    if (g_pir_wakeup_state == PIR_WAKEUP_QUIET)
    {
        g_pir_quiet_ticks = 0U;
        g_pir_wakeup_state = PIR_WAKEUP_WAIT_LOW;
    }
    else if (g_pir_wakeup_state == PIR_WAKEUP_ARMED)
    {
        if ((flags & PIR_CHANGED_LEFT) != 0U)
        {
            g_pir_left_high_ticks = 0U;
        }
        if ((flags & PIR_CHANGED_RIGHT) != 0U)
        {
            g_pir_right_high_ticks = 0U;
        }
    }
    __set_PRIMASK(primask);
}

// 停止TIM4计时，清除计数和中断标志，重置状态机
static void PIR_WakeupClear(void)
{
    g_pir_wakeup_state = PIR_WAKEUP_DISABLED;
    g_pir_quiet_ticks = 0U;
    g_pir_left_high_ticks = 0U;
    g_pir_right_high_ticks = 0U;

    /* USB先于PIR初始化，早期的USB回调不能访问未开时钟的TIM4。 */
    if (g_pir_wakeup_initialized != 0U)
    {
        TIM_Enable(TIM4, DISABLE);
        TIM_SetCnt(TIM4, 0U);
        TIM_ClrIntPendingBit(TIM4, TIM_INT_UPDATE);
    }
}
// USB 进入挂起，启动TIM4计时器，监测是否需要唤醒 PC
void PIR_WakeupSuspend(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    PIR_WakeupClear();
    if ((g_pir_wakeup_initialized != 0U) && (g_pir_wakeup_enabled != 0U)
        && (g_pir_wakeup_sent == 0U) && (bDeviceState == SUSPENDED))
    {
        g_pir_wakeup_state = PIR_WAKEUP_WAIT_LOW;
        TIM_Enable(TIM4, ENABLE);
    }
    __set_PRIMASK(primask);
}

// 上层协议调用这个函数来设置是否允许PIR唤醒PC
uint8_t PIR_SetWakeupEnabled(uint8_t enabled)
{
    uint32_t primask;

    if ((enabled > 1U) || (g_pir_wakeup_initialized == 0U))
    {
        return 0U;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    /* 同值重发必须无副作用；切换开关也不能清除本轮已唤醒标志。 */
    if (g_pir_wakeup_enabled != enabled)
    {
        g_pir_wakeup_enabled = enabled;
        PIR_WakeupSuspend();
    }
    __set_PRIMASK(primask);
    return 1U;
}

uint8_t PIR_GetWakeupEnabled(uint8_t *enabled)
{
    if ((enabled == NULL) || (g_pir_wakeup_initialized == 0U))
    {
        return 0U;
    }
    *enabled = g_pir_wakeup_enabled;
    return 1U;
}
// USB 出现唤醒/恢复事件 停止 TIM4，清除检测计时
void PIR_WakeupResume(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    PIR_WakeupClear();
    __set_PRIMASK(primask);
}

// USB 恢复通信后，清理本轮 PIR 唤醒状态，其实就是停止TIM4
void PIR_WakeupOnSof(void)
{
    uint32_t primask;

    if ((g_pir_wakeup_sent == 0U) && (g_pir_wakeup_state == PIR_WAKEUP_DISABLED))
    {
        return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    /* 本机Resume_Init/WKUP不等于主机恢复；收到主机SOF才结束本轮睡眠。 */
    g_pir_wakeup_sent = 0U;
    PIR_WakeupClear();
    __set_PRIMASK(primask);
}


// 这个函数10ms调用一次，TIM4中断服务函数里调用
void PIR_WakeupTimerFromISR(void)
{
    // TIM4在USB 处于 SUSPENDED 状态时，不启动，也就是在主机挂起时才启动
    uint8_t left_state;
    uint8_t right_state;
    uint32_t primask;

    if (TIM_GetIntStatus(TIM4, TIM_INT_UPDATE) == RESET)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    TIM_ClrIntPendingBit(TIM4, TIM_INT_UPDATE);
    if ((bDeviceState != SUSPENDED) || (g_pir_wakeup_state == PIR_WAKEUP_DISABLED))
    {
        PIR_WakeupClear();
        __set_PRIMASK(primask);
        return;
    }

    left_state = (uint8_t)GPIO_ReadInputDataBit(PIR_PORT, PIR_LEFT_PIN);
    right_state = (uint8_t)GPIO_ReadInputDataBit(PIR_PORT, PIR_RIGHT_PIN);

    if (g_pir_wakeup_state != PIR_WAKEUP_ARMED)
    {
        if ((left_state != 0U) || (right_state != 0U))
        {
            g_pir_quiet_ticks = 0U;
            g_pir_wakeup_state = PIR_WAKEUP_WAIT_LOW;
        }
        else if (g_pir_wakeup_state == PIR_WAKEUP_WAIT_LOW)
        {
            /* 第一次观察到LOW只确定起点，不计入一个完整周期。 */
            g_pir_wakeup_state = PIR_WAKEUP_QUIET;
        }
        else if (++g_pir_quiet_ticks >= (PIR_WAKEUP_QUIET_TIME_SEC * 1000U / PIR_WAKEUP_TICK_MS))
        {
            g_pir_wakeup_state = PIR_WAKEUP_ARMED;
        }
    }
    else
    {
        /* 左右独立计时；LOW清零，短HIGH不要求重新进行10秒静默布防。 */
        if (left_state == 0U)
        {
            g_pir_left_high_ticks = 0U;
        }
        else if (g_pir_left_high_ticks <= (PIR_WAKEUP_HIGH_TIME_MS / PIR_WAKEUP_TICK_MS))
        {
            g_pir_left_high_ticks++;
        }
        if (right_state == 0U)
        {
            g_pir_right_high_ticks = 0U;
        }
        else if (g_pir_right_high_ticks <= (PIR_WAKEUP_HIGH_TIME_MS / PIR_WAKEUP_TICK_MS))
        {
            g_pir_right_high_ticks++;
        }

        /* 需要101个HIGH采样点才覆盖完整1000ms，且必须有主机USB唤醒授权。 */
        if (((g_pir_left_high_ticks > (PIR_WAKEUP_HIGH_TIME_MS / PIR_WAKEUP_TICK_MS))
             || (g_pir_right_high_ticks > (PIR_WAKEUP_HIGH_TIME_MS / PIR_WAKEUP_TICK_MS)))
            && ((Device_Info.CurrentFeature & USB_REMOTE_WAKEUP_FEATURE_MASK) != 0U))
        {
            g_pir_wakeup_sent = 1U;
            PIR_WakeupClear();
            /* 检查授权到发起请求之间不允许USB中断改变挂起/授权状态。 */
            USB_Remote_Wakeup();
        }
    }
    __set_PRIMASK(primask);
}

uint8_t PIR_TakeEvent(uint8_t *flags,uint8_t *left,uint8_t *right)
{
    if(flags == NULL || left == NULL || right == NULL)
    return 0;

    if(g_pir_changed_flags == 0U)
    return 0;

    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    *flags = g_pir_changed_flags;
    *left = g_pir_left_snapshot;
    *right = g_pir_right_snapshot;

    g_pir_changed_flags = 0U;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return 1U;
}
