#include "usb_cdc.h"
#include "log.h"
#include "n32l40x.h"
#include "usb_conf.h"
#include "usb_desc.h"
#include "usb_lib.h"
#include "usb_pwr.h"

#if ((USB_CDC_RX_BUFFER_SIZE & (USB_CDC_RX_BUFFER_SIZE - 1U)) != 0U)
#error "USB_CDC_RX_BUFFER_SIZE must be a power of two"
#endif

#if ((USB_CDC_TX_BUFFER_SIZE & (USB_CDC_TX_BUFFER_SIZE - 1U)) != 0U)
#error "USB_CDC_TX_BUFFER_SIZE must be a power of two"
#endif

#define USB_CDC_RX_BUFFER_MASK (USB_CDC_RX_BUFFER_SIZE - 1U)
#define USB_CDC_TX_BUFFER_MASK (USB_CDC_TX_BUFFER_SIZE - 1U)

static uint8_t cdc_rx_buffer[USB_CDC_RX_BUFFER_SIZE];
static volatile uint16_t cdc_rx_write;
static volatile uint16_t cdc_rx_read;

static uint8_t cdc_rx_pending[VIRTUAL_COM_PORT_DATA_SIZE];
static volatile uint16_t cdc_rx_pending_length;

static uint8_t cdc_tx_buffer[USB_CDC_TX_BUFFER_SIZE];
static volatile uint16_t cdc_tx_write;
static volatile uint16_t cdc_tx_read;
static uint8_t cdc_tx_packet[VIRTUAL_COM_PORT_DATA_SIZE];
static volatile uint8_t cdc_tx_busy;
static volatile uint16_t cdc_tx_last_packet_length;

static volatile uint32_t cdc_reset_generation;

static uint16_t USB_CDC_RingCount(uint16_t write,
                                  uint16_t read,
                                  uint16_t mask)
{
    return (uint16_t)((write - read) & mask);
}

static uint16_t USB_CDC_RxFree(void)
{
    return (uint16_t)(USB_CDC_RX_BUFFER_MASK
                      - USB_CDC_RingCount(cdc_rx_write,
                                          cdc_rx_read,
                                          USB_CDC_RX_BUFFER_MASK));
}

static uint16_t USB_CDC_TxFree(void)
{
    return (uint16_t)(USB_CDC_TX_BUFFER_MASK
                      - USB_CDC_RingCount(cdc_tx_write,
                                          cdc_tx_read,
                                          USB_CDC_TX_BUFFER_MASK));
}

static void USB_CDC_CopyToRing(uint8_t* ring,
                               uint16_t mask,
                               uint16_t write,
                               const uint8_t* data,
                               uint16_t length)
{
    uint16_t i;

    for (i = 0; i < length; i++)
    {
        ring[write] = data[i];
        write = (uint16_t)((write + 1U) & mask);
    }
}

static void USB_CDC_TryResumeRx(void)
{
    uint32_t primask;
    uint16_t length;
    uint16_t write;

    primask = __get_PRIMASK();
    __disable_irq();

    length = cdc_rx_pending_length;
    if ((length != 0U) && (USB_CDC_RxFree() >= length))
    {
        write = cdc_rx_write;
        USB_CDC_CopyToRing(cdc_rx_buffer,
                           USB_CDC_RX_BUFFER_MASK,
                           write,
                           cdc_rx_pending,
                           length);
        __DMB();
        cdc_rx_write = (uint16_t)((write + length) & USB_CDC_RX_BUFFER_MASK);
        cdc_rx_pending_length = 0U;

        if (bDeviceState == CONFIGURED)
        {
            USB_SetEpRxValid(ENDP3);
        }
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
}

static void USB_CDC_SendNextPacket(void)
{
    uint16_t count;
    uint16_t length;
    uint16_t read;
    uint16_t i;

    if ((cdc_tx_busy != 0U) || (bDeviceState != CONFIGURED))
    {
        return;
    }

    count = USB_CDC_RingCount(cdc_tx_write,
                              cdc_tx_read,
                              USB_CDC_TX_BUFFER_MASK);
    if (count == 0U)
    {
        return;
    }

    length = count;
    if (length > VIRTUAL_COM_PORT_DATA_SIZE)
    {
        length = VIRTUAL_COM_PORT_DATA_SIZE;
    }

    read = cdc_tx_read;
    for (i = 0; i < length; i++)
    {
        cdc_tx_packet[i] = cdc_tx_buffer[read];
        read = (uint16_t)((read + 1U) & USB_CDC_TX_BUFFER_MASK);
    }

    cdc_tx_read = read;
    cdc_tx_last_packet_length = length;
    cdc_tx_busy = 1U;

    USB_CopyUserToPMABuf(cdc_tx_packet, ENDP1_TXADDR, length);
    USB_SetEpTxCnt(ENDP1, length);
    USB_SetEpTxValid(ENDP1);
}

void USB_CDC_Init(void)
{
    USB_CDC_Reset();
}

void USB_CDC_Reset(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    cdc_reset_generation++;
    cdc_rx_write = 0U;
    cdc_rx_read = 0U;
    cdc_rx_pending_length = 0U;
    cdc_tx_write = 0U;
    cdc_tx_read = 0U;
    cdc_tx_busy = 0U;
    cdc_tx_last_packet_length = 0U;

    if (primask == 0U)
    {
        __enable_irq();
    }
}

uint16_t USB_CDC_GetRxCount(void)
{
    return USB_CDC_RingCount(cdc_rx_write,
                             cdc_rx_read,
                             USB_CDC_RX_BUFFER_MASK);
}

uint16_t USB_CDC_Read(uint8_t* data, uint16_t length)
{
    uint32_t generation;
    uint32_t primask;
    uint16_t available;
    uint16_t read;
    uint16_t i;

    if ((data == 0) || (length == 0U))
    {
        return 0U;
    }

    generation = cdc_reset_generation;
    available = USB_CDC_GetRxCount();
    if (length > available)
    {
        length = available;
    }

    read = cdc_rx_read;
    for (i = 0; i < length; i++)
    {
        data[i] = cdc_rx_buffer[read];
        read = (uint16_t)((read + 1U) & USB_CDC_RX_BUFFER_MASK);
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (generation != cdc_reset_generation)
    {
        length = 0U;
    }
    else
    {
        cdc_rx_read = read;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }

    USB_CDC_TryResumeRx();
    return length;
}

uint16_t USB_CDC_GetTxFree(void)
{
    return USB_CDC_TxFree();
}

bool USB_CDC_Write(const uint8_t* data, uint16_t length)
{
    uint32_t generation;
    uint32_t primask;
    uint16_t write;
    bool result;

    if (length == 0U)
    {
        return true;
    }
    if ((data == 0) || (bDeviceState != CONFIGURED))
    {
        printf("USB_CDC_Write: Invalid parameters\n");
        return false;
    }

    generation = cdc_reset_generation;
    if (USB_CDC_TxFree() < length)
    {
        printf("USB_CDC_Write: buffer full\n");
        return false;
    }

    write = cdc_tx_write;
    USB_CDC_CopyToRing(cdc_tx_buffer,
                       USB_CDC_TX_BUFFER_MASK,
                       write,
                       data,
                       length);

    result = false;
    primask = __get_PRIMASK();
    __disable_irq();
    if ((generation == cdc_reset_generation)
        && (bDeviceState == CONFIGURED))
    {
        __DMB();
        cdc_tx_write = (uint16_t)((write + length) & USB_CDC_TX_BUFFER_MASK);
        result = true;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }

    return result;
}

bool USB_CDC_OnRxPacket(const uint8_t* data, uint16_t length)
{
    uint16_t write;

    if ((data == 0) || (length == 0U))
    {
        return true;
    }

    if ((length > VIRTUAL_COM_PORT_DATA_SIZE)
        || (cdc_rx_pending_length != 0U))
    {
        return false;
    }

    if (USB_CDC_RxFree() < length)
    {
        USB_CDC_CopyToRing(cdc_rx_pending,
                           (VIRTUAL_COM_PORT_DATA_SIZE - 1U),
                           0U,
                           data,
                           length);
        __DMB();
        cdc_rx_pending_length = length;
        return false;
    }

    write = cdc_rx_write;
    USB_CDC_CopyToRing(cdc_rx_buffer,
                       USB_CDC_RX_BUFFER_MASK,
                       write,
                       data,
                       length);
    __DMB();
    cdc_rx_write = (uint16_t)((write + length) & USB_CDC_RX_BUFFER_MASK);
    return true;
}

void USB_CDC_TxProcess(void)
{
    USB_CDC_SendNextPacket();
}

void USB_CDC_TxComplete(void)
{
    uint16_t count;

    if (cdc_tx_busy == 0U)
    {
        return;
    }

    cdc_tx_busy = 0U;
    count = USB_CDC_RingCount(cdc_tx_write,
                              cdc_tx_read,
                              USB_CDC_TX_BUFFER_MASK);

    if ((cdc_tx_last_packet_length == VIRTUAL_COM_PORT_DATA_SIZE)
        && (count == 0U))
    {
        /* Terminate a transfer ending on a full-size packet with a ZLP. */
        cdc_tx_last_packet_length = 0U;
        cdc_tx_busy = 1U;
        USB_SetEpTxCnt(ENDP1, 0U);
        USB_SetEpTxValid(ENDP1);
        return;
    }

    cdc_tx_last_packet_length = 0U;
    USB_CDC_SendNextPacket();
}
