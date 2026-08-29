#ifndef __USB_CDC_H
#define __USB_CDC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ring capacity is one byte less than the configured buffer size. */
#define USB_CDC_RX_BUFFER_SIZE 2048U
#define USB_CDC_TX_BUFFER_SIZE 2048U

void USB_CDC_Init(void);
void USB_CDC_Reset(void);

uint16_t USB_CDC_GetRxCount(void);
/* Returns the number of bytes copied from the receive ring. */
uint16_t USB_CDC_Read(uint8_t* data, uint16_t length);

uint16_t USB_CDC_GetTxFree(void);
/* Non-blocking and all-or-nothing: false means no data was queued. */
bool USB_CDC_Write(const uint8_t* data, uint16_t length);

/* USB endpoint callbacks. Do not call these functions from application code. */
bool USB_CDC_OnRxPacket(const uint8_t* data, uint16_t length);
void USB_CDC_TxProcess(void);
void USB_CDC_TxComplete(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_CDC_H */
