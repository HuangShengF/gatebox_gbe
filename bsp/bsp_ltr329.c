#include "bsp_ltr329.h"

#include "n32l40x.h"
#include "n32l40x_i2c.h"

#define I2CT_FLAG_TIMEOUT ((uint32_t)0x1000)
#define I2CT_LONG_TIMEOUT ((uint32_t)(10 * I2CT_FLAG_TIMEOUT))
#define I2C_MASTER_ADDR 0x30
/* N32 I2C发送地址接口使用左移后的8位地址格式 */
#define I2C_SLAVE_ADDR ((uint8_t)(LTR329_I2C_ADDR << 1))

typedef enum
{
    C_READY = 0,
    C_START_BIT,
    C_STOP_BIT
} CommCtrl_t;

typedef enum
{
    MASTER_BUSY = 0,
    MASTER_MODE,
    MASTER_TXMODE,
    MASTER_RXMODE,
    MASTER_SENDING,
    MASTER_SENDED,
    MASTER_RECVD,
    MASTER_BYTEF
} ErrCode_t;

#ifdef NON_REENTRANT
static uint32_t Mutex_Flag = 0;
#endif

#define I2Cx I2C1
#define I2Cx_SCL_PIN GPIO_PIN_6
#define I2Cx_SDA_PIN GPIO_PIN_7
#define GPIOx GPIOB

static __IO uint32_t I2CTimeout;
static CommCtrl_t Comm_Flag = C_READY;

static void CommTimeOut_CallBack(ErrCode_t errcode);
static void LTR329_ConfigSclMode(GPIO_ModeType mode);

int8_t LTR329_Init(uint8_t gain, uint8_t int_time, uint8_t meas_rate)
{
    delay_ms(250);
    uint8_t id = 0;
    i2c_master_init();

    // 检查ID,器件是否存在
    LTR329_ReadRegs(LTR329_REG_MANUFAC_ID, &id, 1);
    if (id != 0x05)
    {
        return LTR329_ERR_ID;
    }

    LTR329_WriteReg(LTR329_REG_ALS_CONTR, gain | 0x01);

    LTR329_WriteReg(LTR329_REG_MEAS_RATE, meas_rate | int_time);
    delay_ms(100);
    return LTR329_OK;
}

uint8_t LTR329_ReadRawData(uint16_t *ch0, uint16_t *ch1)
{
    if (ch0 == NULL || ch1 == NULL)
    {
        return LTR329_ERR_DATA_INVALID;
    }
    uint8_t status = 0;
    uint8_t buf[4] = {0};
    LTR329_ReadRegs(LTR329_REG_STATUS, &status, 1);
    if (((status & LTR329_STATUS_DATA_VALID) == 0) && (status & LTR329_STATUS_NEW_DATA))
    {
       uint8_t ret = LTR329_ReadRegs(LTR329_REG_DATA_CH1_0, buf, 4);
       if(ret != 0) // I2C通信失败
       {
              return LTR329_ERR_I2C;
       }
        *ch1 = (buf[1] << 8) | buf[0];
        *ch0 = (buf[3] << 8) | buf[2];
        return LTR329_OK;
    }
    return LTR329_ERR_DATA_INVALID;
}

float LTR329_CalculateLux( uint8_t gain, uint8_t int_time, float pFactor)
{
    uint16_t ch0 = 0;
    uint16_t ch1 = 0;
    LTR329_ReadRawData(&ch0, &ch1);
    
    float ratio;
    float lux;
    float gain_factor;
    float time_factor;

    // 零照度保护，防止除零
    if ((ch0 + ch1) == 0) {
        return 0.0f;
    }

    // 1. 计算增益系数
    switch (gain) {
        case LTR329_GAIN_1X:  gain_factor = 1.0f;  break;
        case LTR329_GAIN_2X:  gain_factor = 2.0f;  break;
        case LTR329_GAIN_4X:  gain_factor = 4.0f;  break;
        case LTR329_GAIN_8X:  gain_factor = 8.0f;  break;
        case LTR329_GAIN_48X: gain_factor = 48.0f; break;
        case LTR329_GAIN_96X: gain_factor = 96.0f; break;
        default:              gain_factor = 1.0f;  break;
    }

    // 2. 计算积分时间系数（以100ms为基准1.0）
    switch (int_time) {
        case LTR329_INT_50MS:  time_factor = 0.5f;  break;
        case LTR329_INT_100MS: time_factor = 1.0f;  break;
        case LTR329_INT_150MS: time_factor = 1.5f;  break;
        case LTR329_INT_200MS: time_factor = 2.0f;  break;
        case LTR329_INT_250MS: time_factor = 2.5f;  break;
        case LTR329_INT_300MS: time_factor = 3.0f;  break;
        case LTR329_INT_350MS: time_factor = 3.5f;  break;
        case LTR329_INT_400MS: time_factor = 4.0f;  break;
        default:               time_factor = 1.0f;  break;
    }

    // 3. 计算红外占比 ratio = CH1 / (CH0 + CH1)  官方标准定义
    ratio = (float)ch1 / (float)(ch0 + ch1);

    // 4. 官方附录A 三档分段公式
    if (ratio < 0.45f) {
        lux = (1.7743f * ch0 + 1.1059f * ch1);
    }
    else if (ratio < 0.64f) {
        lux = (4.2785f * ch0 - 1.9548f * ch1);
    }
    else if (ratio < 0.85f) {
        lux = (0.5926f * ch0 + 0.1185f * ch1);
    }
    else {
        // 红外占比过高，可见光分量可忽略，返回0
        return 0.0f;
    }

    // 5. 归一化：除以增益、积分时间、窗口衰减系数
    lux = lux / gain_factor / time_factor / pFactor;

    // 防止出现负值
    return (lux > 0.0f) ? lux : 0.0f;


    return lux;
}

int LTR329_WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t data[2];

    data[0] = reg;
    data[1] = value;

    return i2c_master_send(data, 2);
}
int LTR329_ReadRegs(uint8_t reg, uint8_t *data, uint16_t len)
{
    ErrCode_t errcode;

    if ((data == 0) || (len == 0))
    {
        return -1;
    }

#ifdef NON_REENTRANT
    if (Mutex_Flag)
    {
        return -1;
    }
    Mutex_Flag = 1;
#endif

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (I2C_GetFlag(I2C1, I2C_FLAG_BUSY))
    {
        if ((I2CTimeout--) == 0)
        {
            errcode = MASTER_BUSY;
            goto read_error;
        }
    }

    I2C_ConfigNackLocation(I2C1, I2C_NACK_POS_CURRENT);
    I2C_ConfigAck(I2C1, ENABLE);
    I2C_GenerateStart(I2C1, ENABLE);

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_MODE_FLAG))
    {
        if ((I2CTimeout--) == 0)
        {
            errcode = MASTER_MODE;
            goto read_error;
        }
    }

    I2C_SendAddr7bit(I2C1, I2C_SLAVE_ADDR, I2C_DIRECTION_SEND);

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_TXMODE_FLAG))
    {
        if ((I2CTimeout--) == 0)
        {
            errcode = MASTER_TXMODE;
            goto read_error;
        }
    }

    I2C_SendData(I2C1, reg);

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_DATA_SENDED))
    {
        if ((I2CTimeout--) == 0)
        {
            errcode = MASTER_SENDED;
            goto read_error;
        }
    }

    I2C_GenerateStart(I2C1, ENABLE);

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_MODE_FLAG))
    {
        if ((I2CTimeout--) == 0)
        {
            errcode = MASTER_MODE;
            goto read_error;
        }
    }

    I2C_SendAddr7bit(I2C1, I2C_SLAVE_ADDR, I2C_DIRECTION_RECV);

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_GetFlag(I2C1, I2C_FLAG_ADDRF))
    {
        if ((I2CTimeout--) == 0)
        {
            errcode = MASTER_RXMODE;
            goto read_error;
        }
    }

    if (len == 1)
    {
        I2C_ConfigAck(I2C1, DISABLE);
        (void)I2C1->STS1;
        (void)I2C1->STS2;
        I2C_GenerateStop(I2C1, ENABLE);

        I2CTimeout = I2CT_LONG_TIMEOUT;
        while (!I2C_GetFlag(I2C1, I2C_FLAG_RXDATNE))
        {
            if ((I2CTimeout--) == 0)
            {
                errcode = MASTER_RECVD;
                goto read_error;
            }
        }

        *data = I2C_RecvData(I2C1);
    }
    else if (len == 2)
    {
        I2C_ConfigNackLocation(I2C1, I2C_NACK_POS_NEXT);
        (void)I2C1->STS1;
        (void)I2C1->STS2;
        I2C_ConfigAck(I2C1, DISABLE);

        I2CTimeout = I2CT_LONG_TIMEOUT;
        while (!I2C_GetFlag(I2C1, I2C_FLAG_BYTEF))
        {
            if ((I2CTimeout--) == 0)
            {
                errcode = MASTER_BYTEF;
                goto read_error;
            }
        }

        I2C_GenerateStop(I2C1, ENABLE);
        *data++ = I2C_RecvData(I2C1);
        *data = I2C_RecvData(I2C1);
    }
    else
    {
        (void)I2C1->STS1;
        (void)I2C1->STS2;

        while (len > 3)
        {
            I2CTimeout = I2CT_LONG_TIMEOUT;
            while (!I2C_GetFlag(I2C1, I2C_FLAG_RXDATNE))
            {
                if ((I2CTimeout--) == 0)
                {
                    errcode = MASTER_RECVD;
                    goto read_error;
                }
            }

            *data++ = I2C_RecvData(I2C1);
            len--;
        }

        I2CTimeout = I2CT_LONG_TIMEOUT;
        while (!I2C_GetFlag(I2C1, I2C_FLAG_BYTEF))
        {
            if ((I2CTimeout--) == 0)
            {
                errcode = MASTER_BYTEF;
                goto read_error;
            }
        }

        I2C_ConfigAck(I2C1, DISABLE);
        *data++ = I2C_RecvData(I2C1);

        I2CTimeout = I2CT_LONG_TIMEOUT;
        while (!I2C_GetFlag(I2C1, I2C_FLAG_BYTEF))
        {
            if ((I2CTimeout--) == 0)
            {
                errcode = MASTER_BYTEF;
                goto read_error;
            }
        }

        /*
         * N32L40x I2C errata workaround:
         * hold SCL low until data N-1 is read, then release SCL to generate STOP.
         */
        GPIO_ResetBits(GPIOx, I2Cx_SCL_PIN);
        LTR329_ConfigSclMode(GPIO_Mode_Out_OD);
        I2C_GenerateStop(I2C1, ENABLE);
        *data++ = I2C_RecvData(I2C1);
        LTR329_ConfigSclMode(GPIO_Mode_AF_OD);
        *data = I2C_RecvData(I2C1);
    }

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (I2C_GetFlag(I2C1, I2C_FLAG_BUSY))
    {
        if ((I2CTimeout--) == 0)
        {
            errcode = MASTER_BUSY;
            goto read_error;
        }
    }

    I2C_ConfigAck(I2C1, ENABLE);
    I2C_ConfigNackLocation(I2C1, I2C_NACK_POS_CURRENT);
    Comm_Flag = C_READY;

#ifdef NON_REENTRANT
    Mutex_Flag = 0;
#endif

    return 0;

read_error:
    CommTimeOut_CallBack(errcode);
    return 1;
}

int i2c_master_init(void)
{
    I2C_InitType i2c1_master;
    GPIO_InitType i2c1_gpio;
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_I2C1, ENABLE);
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE);
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB, ENABLE);
    GPIOx->POD |= (I2Cx_SCL_PIN | I2Cx_SDA_PIN); // pull up pin

    /*PB6 -- SCL; PB7 -- SDA*/
    GPIO_InitStruct(&i2c1_gpio);
    i2c1_gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    i2c1_gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
    i2c1_gpio.GPIO_Mode = GPIO_Mode_AF_OD;
    i2c1_gpio.GPIO_Alternate = GPIO_AF1_I2C1;
    i2c1_gpio.GPIO_Pull = GPIO_Pull_Up;
    GPIO_InitPeripheral(GPIOB, &i2c1_gpio);

    I2C_DeInit(I2C1);
    I2C_InitStruct(&i2c1_master);
    i2c1_master.BusMode = I2C_BUSMODE_I2C;
    i2c1_master.FmDutyCycle = I2C_FMDUTYCYCLE_2;
    i2c1_master.OwnAddr1 = I2C_MASTER_ADDR;
    i2c1_master.AckEnable = I2C_ACKEN;
    i2c1_master.AddrMode = I2C_ADDR_MODE_7BIT;
    i2c1_master.ClkSpeed = 100000; // 100K

    I2C_Init(I2C1, &i2c1_master);
    I2C_Enable(I2C1, ENABLE);
    return 0;
}

int i2c_master_send(uint8_t *data, int len)
{
    uint8_t *sendBufferPtr = data;

#ifdef NON_REENTRANT
    if (Mutex_Flag)
        return -1;
    else
        Mutex_Flag = 1;
#endif

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (I2C_GetFlag(I2C1, I2C_FLAG_BUSY))
    {
        if ((I2CTimeout--) == 0)
        {
            CommTimeOut_CallBack(MASTER_BUSY);
            return 1;
        }
    }

    if (Comm_Flag == C_READY)
    {
        Comm_Flag = C_START_BIT;
        I2C_GenerateStart(I2C1, ENABLE);
    }

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_MODE_FLAG)) // EV5
    {
        if ((I2CTimeout--) == 0)
        {
            CommTimeOut_CallBack(MASTER_MODE);
            return 1;
        }
    }

    I2C_SendAddr7bit(I2C1, I2C_SLAVE_ADDR, I2C_DIRECTION_SEND);
    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_TXMODE_FLAG)) // EV6
    {
        if ((I2CTimeout--) == 0)
        {
            CommTimeOut_CallBack(MASTER_TXMODE);
            return 1;
        }
    }
    Comm_Flag = C_READY;

    // send data
    while (len-- > 0)
    {
        I2C_SendData(I2C1, *sendBufferPtr++);
        I2CTimeout = I2CT_LONG_TIMEOUT;
        while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_DATA_SENDING)) // EV8
        {
            if ((I2CTimeout--) == 0)
            {
                CommTimeOut_CallBack(MASTER_SENDING);
                return 1;
            }
        }
    }

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_DATA_SENDED)) // EV8-2
    {
        if ((I2CTimeout--) == 0)
        {
            CommTimeOut_CallBack(MASTER_SENDED);
            return 1;
        }
    }

    if (Comm_Flag == C_READY)
    {
        Comm_Flag = C_STOP_BIT;
        I2C_GenerateStop(I2C1, ENABLE);
    }

    while (I2C_GetFlag(I2C1, I2C_FLAG_BUSY))
    {
        if ((I2CTimeout--) == 0)
        {
            CommTimeOut_CallBack(MASTER_BUSY);
            return 1;
        }
    }
    Comm_Flag = C_READY;

#ifdef NON_REENTRANT
    if (Mutex_Flag)
        Mutex_Flag = 0;
    else
        return -2;
#endif

    return 0;
}

int i2c_master_recv(uint8_t *data, int len)
{
    uint8_t *recvBufferPtr = data;

#ifdef NON_REENTRANT
    if (Mutex_Flag)
        return -1;
    else
        Mutex_Flag = 1;
#endif

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (I2C_GetFlag(I2C1, I2C_FLAG_BUSY))
    {
        if ((I2CTimeout--) == 0)
        {
            CommTimeOut_CallBack(MASTER_BUSY);
            return 1;
        }
    }
    I2C_ConfigAck(I2C1, ENABLE);

    // send start
    if (Comm_Flag == C_READY)
    {
        Comm_Flag = C_START_BIT;
        I2C_GenerateStart(I2C1, ENABLE);
    }

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_MODE_FLAG)) // EV5
    {
        if ((I2CTimeout--) == 0)
        {
            CommTimeOut_CallBack(MASTER_MODE);
            return 1;
        }
    }

    // send addr
    I2C_SendAddr7bit(I2C1, I2C_SLAVE_ADDR, I2C_DIRECTION_RECV);
    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_RXMODE_FLAG)) // EV6
    {
        if ((I2CTimeout--) == 0)
        {
            CommTimeOut_CallBack(MASTER_RXMODE);
            return 1;
        }
    }
    Comm_Flag = C_READY;

    if (len == 1)
    {
        I2C_ConfigAck(I2C1, DISABLE);
        (void)(I2C1->STS1); /// clear ADDR
        (void)(I2C1->STS2);
        if (Comm_Flag == C_READY)
        {
            Comm_Flag = C_STOP_BIT;
            I2C_GenerateStop(I2C1, ENABLE);
        }

        I2CTimeout = I2CT_LONG_TIMEOUT;
        while (!I2C_GetFlag(I2Cx, I2C_FLAG_RXDATNE))
        {
            if ((I2CTimeout--) == 0)
            {
                CommTimeOut_CallBack(MASTER_RECVD);
                return 1;
            }
        }
        *recvBufferPtr++ = I2C_RecvData(I2C1);
        len--;
    }
    else if (len == 2)
    {
        I2C1->CTRL1 |= 0x0800; /// set ACKPOS
        (void)(I2C1->STS1);
        (void)(I2C1->STS2);
        I2C_ConfigAck(I2C1, DISABLE);

        I2CTimeout = I2CT_LONG_TIMEOUT;
        while (!I2C_GetFlag(I2C1, I2C_FLAG_BYTEF))
        {
            if ((I2CTimeout--) == 0)
            {
                CommTimeOut_CallBack(MASTER_BYTEF);
                return 1;
            }
        }

        if (Comm_Flag == C_READY)
        {
            Comm_Flag = C_STOP_BIT;
            I2C_GenerateStop(I2C1, ENABLE);
        }

        *recvBufferPtr++ = I2C_RecvData(I2C1);
        len--;
        *recvBufferPtr++ = I2C_RecvData(I2C1);
        len--;
    }
    else
    {
        I2C_ConfigAck(I2C1, ENABLE);
        (void)(I2C1->STS1);
        (void)(I2C1->STS2);

        while (len)
        {
            if (len == 3)
            {
                I2CTimeout = I2CT_LONG_TIMEOUT;
                while (!I2C_GetFlag(I2C1, I2C_FLAG_BYTEF))
                {
                    if ((I2CTimeout--) == 0)
                    {
                        CommTimeOut_CallBack(MASTER_BYTEF);
                        return 1;
                    }
                }
                I2C_ConfigAck(I2C1, DISABLE);
                *recvBufferPtr++ = I2C_RecvData(I2C1);
                len--;

                I2CTimeout = I2CT_LONG_TIMEOUT;
                while (!I2C_GetFlag(I2C1, I2C_FLAG_BYTEF))
                {
                    if ((I2CTimeout--) == 0)
                    {
                        CommTimeOut_CallBack(MASTER_BYTEF);
                        return 1;
                    }
                }

                if (Comm_Flag == C_READY)
                {
                    Comm_Flag = C_STOP_BIT;
                    I2C_GenerateStop(I2C1, ENABLE);
                }

                *recvBufferPtr++ = I2C_RecvData(I2C1);
                len--;
                *recvBufferPtr++ = I2C_RecvData(I2C1);
                len--;

                break;
            }

            I2CTimeout = I2CT_LONG_TIMEOUT;
            while (!I2C_CheckEvent(I2C1, I2C_EVT_MASTER_DATA_RECVD_FLAG)) // EV7
            {
                if ((I2CTimeout--) == 0)
                {
                    CommTimeOut_CallBack(MASTER_RECVD);
                    return 1;
                }
            }
            *recvBufferPtr++ = I2C_RecvData(I2C1);
            len--;
        }
    }

    I2CTimeout = I2CT_LONG_TIMEOUT;
    while (I2C_GetFlag(I2C1, I2C_FLAG_BUSY))
    {
        if ((I2CTimeout--) == 0)
        {
            CommTimeOut_CallBack(MASTER_BUSY);
            return 1;
        }
    }
    Comm_Flag = C_READY;

#ifdef NON_REENTRANT
    if (Mutex_Flag)
        Mutex_Flag = 0;
    else
        return -2;
#endif

    return 0;
}

static void CommTimeOut_CallBack(ErrCode_t errcode)
{
    (void)errcode;

    if (I2C_GetFlag(I2C1, I2C_FLAG_MSMODE))
    {
        I2C_GenerateStop(I2C1, ENABLE);
    }

    I2C_ConfigAck(I2C1, ENABLE);
    I2C_ConfigNackLocation(I2C1, I2C_NACK_POS_CURRENT);
    Comm_Flag = C_READY;

#ifdef NON_REENTRANT
    Mutex_Flag = 0;
#endif
}

static void LTR329_ConfigSclMode(GPIO_ModeType mode)
{
    GPIO_InitType gpio;

    GPIO_InitStruct(&gpio);
    gpio.Pin = I2Cx_SCL_PIN;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
    gpio.GPIO_Mode = mode;
    gpio.GPIO_Alternate = GPIO_AF1_I2C1;
    gpio.GPIO_Pull = GPIO_Pull_Up;
    GPIO_InitPeripheral(GPIOx, &gpio);
}
