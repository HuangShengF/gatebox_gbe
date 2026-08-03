#include "bsp_lre329.h"

#include "n32l40x.h"
#include "n32l40x_i2c.h"
#include "main.h"
#include "log.h"


#define TEST_BUFFER_SIZE  100
#define I2CT_FLAG_TIMEOUT ((uint32_t)0x1000)
#define I2CT_LONG_TIMEOUT ((uint32_t)(10 * I2CT_FLAG_TIMEOUT))
#define I2C_MASTER_ADDR   0x30
#define LTR329_I2C_ADDR      0x29

#ifdef NON_REENTRANT
static uint32_t Mutex_Flag = 0;
#endif

#define I2C1_TEST
#define I2C1_REMAP
#define I2Cx I2C1
#define I2Cx_SCL_PIN GPIO_PIN_6
#define I2Cx_SDA_PIN GPIO_PIN_7
#define GPIOx        GPIOB

uint8_t tx_buf[TEST_BUFFER_SIZE] = {0};
uint8_t rx_buf[TEST_BUFFER_SIZE] = {0};
volatile Status test_status      = FAILED;
static __IO uint32_t I2CTimeout;
static CommCtrl_t Comm_Flag = C_READY;

Status Buffercmp(uint8_t* pBuffer1, uint8_t* pBuffer2, uint16_t BufferLength);
void Memset(void* s, uint8_t c, uint32_t count);
void CommTimeOut_CallBack(ErrCode_t errcode);

void Delay(uint32_t nCount)
{
   uint32_t tcnt;
   while(nCount--)
   {
       tcnt = 64000 / 5;
       while (tcnt--){;}
   }
}

void Delay_us(uint32_t nCount)
{
   uint32_t tcnt;
   while (nCount--)
   {
       tcnt = 64 / 5;
       while (tcnt--){;}
   }
}

int i2c_master_init(void)
{
   I2C_InitType i2c1_master;
   GPIO_InitType i2c1_gpio;
   RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_I2C1, ENABLE);
   RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE);
   RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB, ENABLE);
   GPIOx->POD |= (I2Cx_SCL_PIN | I2Cx_SDA_PIN);//pull up pin

   /*PB8 -- SCL; PB9 -- SDA*/
   GPIO_InitStruct(&i2c1_gpio);
   i2c1_gpio.Pin               = GPIO_PIN_6 | GPIO_PIN_7;
   i2c1_gpio.GPIO_Slew_Rate    = GPIO_Slew_Rate_High;
   i2c1_gpio.GPIO_Mode         = GPIO_Mode_AF_OD;
   i2c1_gpio.GPIO_Alternate    = GPIO_AF1_I2C1;
   i2c1_gpio.GPIO_Pull         = GPIO_Pull_Up;	  
   GPIO_InitPeripheral(GPIOB, &i2c1_gpio);

   I2C_DeInit(I2C1);
	I2C_InitStruct(&i2c1_master);
   i2c1_master.BusMode     = I2C_BUSMODE_I2C;
   i2c1_master.FmDutyCycle = I2C_FMDUTYCYCLE_2;
   i2c1_master.OwnAddr1    = I2C_MASTER_ADDR;
   i2c1_master.AckEnable   = I2C_ACKEN;
   i2c1_master.AddrMode    = I2C_ADDR_MODE_7BIT;
   i2c1_master.ClkSpeed    = 100000; // 100K

   I2C_Init(I2C1, &i2c1_master);
   I2C_Enable(I2C1, ENABLE);
   return 0;
}

int i2c_master_send(uint8_t* data, int len)
{
   uint8_t* sendBufferPtr = data;
   
#ifdef NON_REENTRANT
   if (Mutex_Flag)
       return -1;
   else
       Mutex_Flag = 1;
#endif
   
   I2CTimeout             = I2CT_LONG_TIMEOUT;
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

int i2c_master_recv(uint8_t* data, int len)
{
    uint8_t* recvBufferPtr = data;
    
#ifdef NON_REENTRANT
    if (Mutex_Flag)
        return -1;
    else
        Mutex_Flag = 1;
#endif
    
    I2CTimeout             = I2CT_LONG_TIMEOUT;
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
