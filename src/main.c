/*****************************************************************************
 * Copyright (c) 2022, Nations Technologies Inc.
 *
 * All rights reserved.
 * ****************************************************************************
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Nations' name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY NATIONS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL NATIONS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ****************************************************************************/

/**
 * @file main.c
 * @author Nations
 * @version V1.2.2
 *
 * @copyright Copyright (c) 2022, Nations Technologies Inc. All rights reserved.
 */
#include "n32l40x.h"
#include "hw_config.h"
#include "usb_lib.h"
#include "usb_pwr.h"
#include "log.h"
#include "delay.h"
#include "bsp_pir.h"
#include "bsp_ir.h"
#include "bsp_ir_example.h"
#include "n32l40x_rcc.h"
#include "gbe_protocol.h"
#include "gb_protocol.h"
#include "usb_cdc.h"
#include "bsp_ltr329.h"
__IO uint32_t TimingDelay = 0;
uint32_t system_clock = 0;

void Delay(__IO uint32_t nCount);
void Protocol_CrcTest(void)
{
    static const uint8_t test_data_1[] =
        {
            0x31, 0x32, 0x33, 0x34, 0x35,
            0x36, 0x37, 0x38, 0x39};

    static const uint8_t test_data_2[] =
        {
            0xA5, 0xB3,
            0x01, 0x00,
            0x01, 0x00,
            0x00, 0x00};

    uint16_t crc_1;
    uint16_t crc_2;

    crc_1 = gb_protocol_crc16(test_data_1, sizeof(test_data_1));
    crc_2 = gb_protocol_crc16(test_data_2, sizeof(test_data_2));

    printf("CRC test 1 = %04X\r\n", (unsigned int)crc_1);
    printf("CRC test 2 = %04X\r\n", (unsigned int)crc_2);
}
static void Clock_Print(void)
{
    RCC_ClocksType clocks;
    uint32_t tim2_clock;

    SystemCoreClockUpdate();
    RCC_GetClocksFreqValue(&clocks);

    /* APB1分频不为1时，TIM2时钟为PCLK1的2倍 */
    if (clocks.Pclk1Freq == clocks.HclkFreq)
    {
        tim2_clock = clocks.Pclk1Freq;
    }
    else
    {
        tim2_clock = clocks.Pclk1Freq * 2;
    }

    printf("SystemCoreClock = %lu Hz\r\n",
           (unsigned long)SystemCoreClock);
    printf("SYSCLK          = %lu Hz\r\n",
           (unsigned long)clocks.SysclkFreq);
    printf("HCLK            = %lu Hz\r\n",
           (unsigned long)clocks.HclkFreq);
    printf("PCLK1           = %lu Hz\r\n",
           (unsigned long)clocks.Pclk1Freq);
    printf("PCLK2           = %lu Hz\r\n",
           (unsigned long)clocks.Pclk2Freq);
    printf("TIM2_CLK        = %lu Hz\r\n",
           (unsigned long)tim2_clock);
    printf("SYSCLK source   = 0x%02X\r\n",
           RCC_GetSysclkSrc());
}
int main(void)
{
    uint8_t cdc_rx_data[64];
    uint16_t cdc_rx_length;
    uint16_t i;
    system_clock = SYSCLK_VALUE_48MHz;

    if (USB_Config(system_clock) == SUCCESS)
    {
        USB_Init();

        while (bDeviceState != CONFIGURED)
        {
        }
    }
    log_init();
    delay_init();
    int ret = LTR329_Init(LTR329_GAIN_1X, LTR329_INT_100MS, LTR329_RATE_200MS);
    if(ret != LTR329_OK)
    {
        printf("LTR329_Init error %d\r\n", ret);
    }
    else 
    {
        printf("LTR329_Init ok\r\n");
    }

    delay_ms(20);
    printf("\r\n");
    Clock_Print();
    PIR_ExtiInit();
    // IR_Init();
    // IR_Start();
    // 初始化红外模块
    IR_Init();

    gbe_protocol_init();
    // uint8_t pir_left_state = 0, pir_right_state = 0;
    // uint16_t ch0 = 0, ch1 = 0;
    // uint8_t left;
    // uint8_t right;
    // uint8_t last_left = 0xFF;
    // uint8_t last_right = 0xFF;
    float lux;
    while (1)
    {
        // printf("hello world\n");
        // IR_Example();
        // delay_xms(2000);
        do
        {
            cdc_rx_length =
                USB_CDC_Read(cdc_rx_data, sizeof(cdc_rx_data));

            for (i = 0; i < cdc_rx_length; i++)
            {
                gb_protocol_process_byte(cdc_rx_data[i]);
            }
        } while (cdc_rx_length != 0U);
        // int ret = LTR329_CalculateLux(LTR329_GAIN_1X, LTR329_INT_100MS, 1.0, &lux);
        // if(ret == LTR329_OK)
        // {
        //     printf("lux: %f\n");
        // }
        // else 
        // {
        //     printf("error: %d\n", ret);
        // }
        
        gbe_protocol_poll();
        // IR_Poll();

        // delay_ms(10);
        // PIR_GetStates(&left, &right);
        // printf(" left=%d, right=%d\r\n", left, right);
        // if ((left != last_left) || (right != last_right))
        // {
        //     printf("PIR raw: left=%d, right=%d\r\n", left, right);
        //     last_left = left;
        //     last_right = right;
        // }

         delay_ms(500);
    }
}

/**
 * @brief  delay count.
 * @param  nCount: delay count cycle.
 */
void Delay(__IO uint32_t nCount)
{
    TimingDelay = nCount;
    for (; nCount != 0; nCount--)
        ;
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param   expr: If expr is false, it calls assert_failed function which reports
 *         the name of the source file and the source line number of the call
 *         that failed. If expr is true, it returns no value.
 * @param  file: pointer to the source file name.
 * @param  line: assert_param error line source number.
 */
void assert_failed(const uint8_t *expr, const uint8_t *file, uint32_t line)
{
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

    /* Infinite loop */
    while (1)
    {
    }
}
#endif
