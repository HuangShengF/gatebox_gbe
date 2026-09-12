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
 * @file usb_prop.c
 * @author Nations
 * @version V1.2.2
 *
 * @copyright Copyright (c) 2022, Nations Technologies Inc. All rights reserved.
 */


/* Includes ------------------------------------------------------------------*/
#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_prop.h"
#include "usb_desc.h"
#include "usb_pwr.h"
#include "usb_cdc.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
uint8_t Request = 0;

static uint8_t hid_idle_rate = 0;
static uint8_t hid_protocol = 1;
static uint8_t hid_input_report[HID_IN_PACKET_SIZE] = {0};
static uint8_t hid_output_report = 0;

LINE_CODING linecoding =
{
    115200, /* baud rate*/
    0x00,   /* stop bits-1*/
    0x00,   /* parity - none*/
    0x08    /* no. of bits 8*/
};

/* -------------------------------------------------------------------------- */
/*  Structures initializations */
/* -------------------------------------------------------------------------- */

USB_Device Device_Table =
{
    EP_NUM,
    1
};

DEVICE_PROP Device_Property =
{
    Virtual_Com_Port_init,
    Virtual_Com_Port_Reset,
    Virtual_Com_Port_Status_In,
    Virtual_Com_Port_Status_Out,
    Virtual_Com_Port_Data_Setup,
    Virtual_Com_Port_NoData_Setup,
    Virtual_Com_Port_Get_Interface_Setting,
    Virtual_Com_Port_GetDeviceDescriptor,
    Virtual_Com_Port_GetConfigDescriptor,
    Virtual_Com_Port_GetStringDescriptor,
    0,
    0x40 /*MAX PACKET SIZE*/
};

USER_STANDARD_REQUESTS User_Standard_Requests =
{
    Virtual_Com_Port_GetConfiguration,
    Virtual_Com_Port_SetConfiguration,
    Virtual_Com_Port_GetInterface,
    Virtual_Com_Port_SetInterface,
    Virtual_Com_Port_GetStatus,
    Virtual_Com_Port_ClearFeature,
    Virtual_Com_Port_SetEndPointFeature,
    Virtual_Com_Port_SetDeviceFeature,
    Virtual_Com_Port_SetDeviceAddress
};

USB_OneDescriptor Device_Descriptor =
{
    (uint8_t*)Virtual_Com_Port_DeviceDescriptor,
    VIRTUAL_COM_PORT_SIZ_DEVICE_DESC
};

USB_OneDescriptor Config_Descriptor =
{
    (uint8_t*)Virtual_Com_Port_ConfigDescriptor,
    VIRTUAL_COM_PORT_SIZ_CONFIG_DESC
};

USB_OneDescriptor HID_Descriptor_Info =
{
    (uint8_t*)HID_Descriptor,
    HID_SIZ_DESC
};

USB_OneDescriptor HID_ReportDescriptor_Info =
{
    (uint8_t*)HID_ReportDescriptor,
    HID_SIZ_REPORT_DESC
};

USB_OneDescriptor String_Descriptor[4] =
{
    {(uint8_t*)Virtual_Com_Port_StringLangID, VIRTUAL_COM_PORT_SIZ_STRING_LANGID},
    {(uint8_t*)Virtual_Com_Port_StringVendor, VIRTUAL_COM_PORT_SIZ_STRING_VENDOR},
    {(uint8_t*)Virtual_Com_Port_StringProduct, VIRTUAL_COM_PORT_SIZ_STRING_PRODUCT},
    {(uint8_t*)Virtual_Com_Port_StringSerial, VIRTUAL_COM_PORT_SIZ_STRING_SERIAL}
};

/* Extern variables ----------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Extern function prototypes ------------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
 * @brief  Virtual COM Port Mouse init routine.
 */
void Virtual_Com_Port_init(void)
{
    pInformation->CurrentConfiguration = 0;

    /* Connect the device */
    PowerOn();

    /* Perform basic device initialization operations */
    USB_SilInit();

    USB_CDC_Init();

    bDeviceState = UNCONNECTED;
}

/**
 * @brief  Virtual_Com_Port Mouse reset routine
 */
void Virtual_Com_Port_Reset(void)
{
    // USB_WakeupReset();
    USB_CDC_Reset();
    hid_idle_rate = 0;
    hid_protocol = 1;
    hid_output_report = 0;

    /* Set Virtual_Com_Port DEVICE as not configured */
    pInformation->CurrentConfiguration = 0;

    /* Current Feature initialization */
    // pInformation->CurrentFeature = Virtual_Com_Port_ConfigDescriptor[7];
    /* Remote wakeup must remain disabled until enabled by the host. */
    pInformation->CurrentFeature = Virtual_Com_Port_ConfigDescriptor[7]
                                   & (uint8_t)(~USB_REMOTE_WAKEUP_FEATURE_MASK);

    // USB复位后默认禁止远程唤醒，等待主机授权
    // pInformation->CurrentFeature = Virtual_Com_Port_ConfigDescriptor[7] & ((uint8_t)~0x20);

    /* Set Virtual_Com_Port DEVICE with the default Interface*/
    pInformation->CurrentInterface = 0;

    USB_SetBuftab(BTABLE_ADDRESS);

    /* Initialize Endpoint 0 */
    USB_SetEpType(ENDP0, EP_CONTROL);
    SetEPTxStatus(ENDP0, EP_TX_STALL);
    USB_SetEpRxAddr(ENDP0, ENDP0_RXADDR);
    USB_SetEpTxAddr(ENDP0, ENDP0_TXADDR);
    USB_ClrStsOut(ENDP0);
    USB_SetEpRxCnt(ENDP0, Device_Property.MaxPacketSize);
    USB_SetEpRxValid(ENDP0);

    /* Initialize Endpoint 1 */
    USB_SetEpType(ENDP1, EP_BULK);
    USB_SetEpTxAddr(ENDP1, ENDP1_TXADDR);
    SetEPTxStatus(ENDP1, EP_TX_NAK);
    SetEPRxStatus(ENDP1, EP_RX_DIS);

    /* Initialize Endpoint 2 */
    USB_SetEpType(ENDP2, EP_INTERRUPT);
    USB_SetEpTxAddr(ENDP2, ENDP2_TXADDR);
    SetEPRxStatus(ENDP2, EP_RX_DIS);
    SetEPTxStatus(ENDP2, EP_TX_NAK);

    /* Initialize Endpoint 3 */
    USB_SetEpType(ENDP3, EP_BULK);
    USB_SetEpRxAddr(ENDP3, ENDP3_RXADDR);
    USB_SetEpRxCnt(ENDP3, VIRTUAL_COM_PORT_DATA_SIZE);
    SetEPRxStatus(ENDP3, EP_RX_VALID);
    SetEPTxStatus(ENDP3, EP_TX_DIS);

    /* Initialize Endpoint 4 */
    USB_SetEpType(ENDP4, EP_INTERRUPT);
    USB_SetEpTxAddr(ENDP4, ENDP4_TXADDR);
    USB_SetEpTxCnt(ENDP4, 0);
    SetEPTxStatus(ENDP4, EP_TX_NAK);
    SetEPRxStatus(ENDP4, EP_RX_DIS);

    /* Set this device to response on default address */
    USB_SetDeviceAddress(0);

    bDeviceState = ATTACHED;
}

/**
 * @brief  Update the device state to configured.
 */
void Virtual_Com_Port_SetConfiguration(void)
{
    USB_DeviceMess *pInfo = &Device_Info;

    if (pInfo->CurrentConfiguration != 0)
    {
        /* Device configured */
        bDeviceState = CONFIGURED;
    }
}

/**
 * @brief  Update the device state to addressed.
 */
void Virtual_Com_Port_SetDeviceAddress (void)
{
    bDeviceState = ADDRESSED;
}

/**
 * @brief  Virtual COM Port Status In Routine.
 */
void Virtual_Com_Port_Status_In(void)
{
    if (Request == SET_LINE_CODING)
    {
        /* CDC is connected directly to the MCU; do not reconfigure USART1. */
        Request = 0;
    }
}

/**
 * @brief  Virtual COM Port Status OUT Routine.
 */
void Virtual_Com_Port_Status_Out(void)
{}

/**
 * @brief   Handle the data class specific requests.
 * @param   RequestNo:Request number.
 * @return  USB_UNSUPPORT or USB_SUCCESS.
 */
USB_Result Virtual_Com_Port_Data_Setup(uint8_t RequestNo)
{
    uint8_t    *(*CopyRoutine)(uint16_t);

    CopyRoutine = NULL;

    if ((RequestNo == GET_DESCRIPTOR)
        && (Type_Recipient == (STANDARD_REQUEST | INTERFACE_RECIPIENT))
        && (pInformation->USBwIndex0 == HID_INTERFACE_NUMBER))
    {
        if (pInformation->USBwValue1 == HID_DESCRIPTOR_TYPE)
        {
            CopyRoutine = HID_GetDescriptor;
        }
        else if (pInformation->USBwValue1 == HID_REPORT_DESCRIPTOR_TYPE)
        {
            CopyRoutine = HID_GetReportDescriptor;
        }
    }
    else if ((Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
             && (pInformation->USBwIndex0 == HID_INTERFACE_NUMBER))
    {
        if (RequestNo == HID_GET_REPORT)
        {
            CopyRoutine = HID_GetReport;
        }
        else if (RequestNo == HID_SET_REPORT)
        {
            CopyRoutine = HID_SetReport;
        }
        else if (RequestNo == HID_GET_IDLE)
        {
            CopyRoutine = HID_GetIdle;
        }
        else if (RequestNo == HID_GET_PROTOCOL)
        {
            CopyRoutine = HID_GetProtocol;
        }
    }
    else if (RequestNo == GET_LINE_CODING)
    {
        if ((Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
            && (pInformation->USBwIndex0 == 0U))
        {
            CopyRoutine = Virtual_Com_Port_GetLineCoding;
        }
    }
    else if (RequestNo == SET_LINE_CODING)
    {
        if ((Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
            && (pInformation->USBwIndex0 == 0U))
        {
            CopyRoutine = Virtual_Com_Port_SetLineCoding;
            Request = SET_LINE_CODING;
        }
    }

    if (CopyRoutine == NULL)
    {
        return UnSupport;
    }

    pInformation->Ctrl_Info.CopyData = CopyRoutine;
    pInformation->Ctrl_Info.Usb_wOffset = 0;
    (*CopyRoutine)(0);
    return Success;
}

/**
 * @brief  handle the no data class specific requests
 * @param  RequestNo:Request number.
 * @return USB_UNSUPPORT or USB_SUCCESS.
 */
USB_Result Virtual_Com_Port_NoData_Setup(uint8_t RequestNo)
{
    if ((Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
        && (pInformation->USBwIndex0 == HID_INTERFACE_NUMBER))
    {
        if (RequestNo == HID_SET_IDLE)
        {
            hid_idle_rate = pInformation->USBwValue1;
            return Success;
        }
        else if ((RequestNo == HID_SET_PROTOCOL)
                 && (pInformation->USBwValue0 <= 1U))
        {
            hid_protocol = pInformation->USBwValue0;
            return Success;
        }
    }
    else if ((Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
             && (pInformation->USBwIndex0 == 0U))
    {
        if (RequestNo == SET_COMM_FEATURE)
        {
            return Success;
        }
        else if (RequestNo == SET_CONTROL_LINE_STATE)
        {
            return Success;
        }
    }

    return UnSupport;
}

/**
 * @brief  Gets the device descriptor.
 * @param  Length: Length.
 * @return The address of the device descriptor.
 */
uint8_t *Virtual_Com_Port_GetDeviceDescriptor(uint16_t Length)
{
    return Standard_GetDescriptorData(Length, &Device_Descriptor);
}

/**
 * @brief  Gets the configuration descriptor.
 * @param  Length:Length.
 * @return The address of the configuration descriptor.
 */
uint8_t *Virtual_Com_Port_GetConfigDescriptor(uint16_t Length)
{
    return Standard_GetDescriptorData(Length, &Config_Descriptor);
}

/**
 * @brief   Gets the string descriptors according to the needed index
 * @param   Length:Length.
 * @return  The address of the string descriptors.
 */
uint8_t *Virtual_Com_Port_GetStringDescriptor(uint16_t Length)
{
    uint8_t wValue0 = pInformation->USBwValue0;
    if (wValue0 >= 4)
    {
        return NULL;
    }
    else
    {
        return Standard_GetDescriptorData(Length, &String_Descriptor[wValue0]);
    }
}

/**
 * @brief   Tests the interface and the alternate setting according to the supported one.
 * @param   Interface:interface number.
 * @param   AlternateSetting : Alternate Setting number.
 * @return  USB_SUCCESS or USB_UNSUPPORT.
 */
USB_Result Virtual_Com_Port_Get_Interface_Setting(uint8_t Interface, uint8_t AlternateSetting)
{
    if (AlternateSetting > 0)
    {
        return UnSupport;
    }
    else if (Interface > HID_INTERFACE_NUMBER)
    {
        return UnSupport;
    }
    return Success;
}

/**
 * @brief   Send the linecoding structure to the PC host.
 * @param   Length: data length.
 * @return  Linecoding structure base address.
 */
uint8_t *Virtual_Com_Port_GetLineCoding(uint16_t Length)
{
    if (Length == 0)
    {
        pInformation->Ctrl_Info.Usb_wLength = sizeof(linecoding);
        return NULL;
    }
    return(uint8_t *)&linecoding;
}

/**
 * @brief   Set the linecoding structure fields.
 * @param   Length: data number.
 * @return  Linecoding structure base address.
 */
uint8_t *Virtual_Com_Port_SetLineCoding(uint16_t Length)
{
    if (Length == 0)
    {
        pInformation->Ctrl_Info.Usb_wLength = sizeof(linecoding);
        return NULL;
    }
    return(uint8_t *)&linecoding;
}

uint8_t *HID_GetDescriptor(uint16_t Length)
{
    return Standard_GetDescriptorData(Length, &HID_Descriptor_Info);
}

uint8_t *HID_GetReportDescriptor(uint16_t Length)
{
    return Standard_GetDescriptorData(Length, &HID_ReportDescriptor_Info);
}

uint8_t *HID_GetReport(uint16_t Length)
{
    if (Length == 0U)
    {
        pInformation->Ctrl_Info.Usb_wLength = sizeof(hid_input_report);
        return NULL;
    }
    return hid_input_report;
}

uint8_t *HID_SetReport(uint16_t Length)
{
    if (Length == 0U)
    {
        pInformation->Ctrl_Info.Usb_wLength = sizeof(hid_output_report);
        return NULL;
    }
    return &hid_output_report;
}

uint8_t *HID_GetIdle(uint16_t Length)
{
    if (Length == 0U)
    {
        pInformation->Ctrl_Info.Usb_wLength = sizeof(hid_idle_rate);
        return NULL;
    }
    return &hid_idle_rate;
}

uint8_t *HID_GetProtocol(uint16_t Length)
{
    if (Length == 0U)
    {
        pInformation->Ctrl_Info.Usb_wLength = sizeof(hid_protocol);
        return NULL;
    }
    return &hid_protocol;
}
