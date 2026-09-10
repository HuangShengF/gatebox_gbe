#include "gbe_protocol.h"
#include "gb_protocol.h"
#include "bsp_pir.h"
#include "bsp_ltr329.h"
#include "log.h"
#include "bsp_ir.h"
#include <string.h>

typedef struct
{
    // GBE_IR_TxState_t state;
    uint16_t sequence;
    uint8_t pending; // 请求已接受，直到完成响应成功入队才清零
    /*
     * 必须是持久内存。
     * IR_SendData()会在TIM6中断中继续访问这里的数据。
     */
    uint8_t ir_tx_data[170];
} GBE_IR_TxContext_t;
static GBE_IR_TxContext_t g_ir_tx;

static void gbe_protocol_pc_request_motion(const Frame_t *frame)
{
    uint8_t response[2];

    if (frame == NULL)
    {
        return;
    }

    if (frame->payload_size != 0U)
    {
        gb_protocol_send_error(frame, ERR_INVALID_PAYLOAD, 0U);
        return;
    }

    PIR_GetStates(&response[0], &response[1]);
    gb_protocol_send_response(frame, response, sizeof(response));
}

static uint8_t gbe_ir_aeha_customer_parity(uint16_t customer_code)
{
    return (uint8_t)((customer_code ^
                      (customer_code >> 4U) ^
                      (customer_code >> 8U) ^
                      (customer_code >> 12U)) &
                     0x0FU);
}

static void gbe_protocol_pc_request_ir_tansimit(const Frame_t *frame)
{
    uint8_t repeat_count;

    if (frame == NULL)
    {
        return;
    }

    if (g_ir_tx.pending || IR_IsSending())
    {
        gb_protocol_send_error(frame, ERR_BUSY, 0U);
        return;
    }
    if (frame->payload == NULL || frame->payload_size < 1U)
    {
        gb_protocol_send_error(frame, ERR_INVALID_PAYLOAD, 0U);
        return;
    }
    // PC发来的数据进行解码然后进行发射
    uint8_t format = frame->payload[0];
    switch (format)
    {
    case IR_PROTOCOL_NEC:
    {
        if (frame->payload_size != 5U)
        {
            gb_protocol_send_error(frame, ERR_INVALID_PAYLOAD, 0U);
            return;
        }
        repeat_count = frame->payload[4];
        if ((frame->payload_size != 5) || (repeat_count == 0))
        {
            gb_protocol_send_error(frame, ERR_INVALID_PAYLOAD, 0U);
            return;
        }

        g_ir_tx.ir_tx_data[0] = frame->payload[1];
        g_ir_tx.ir_tx_data[1] = frame->payload[2];
        g_ir_tx.ir_tx_data[2] = frame->payload[3];
        g_ir_tx.ir_tx_data[3] = (uint8_t)(~frame->payload[3]);
        g_ir_tx.sequence = frame->sequence;

        IR_SendData(IR_PROTOCOL_NEC, g_ir_tx.ir_tx_data, 32, repeat_count); // 最后一个参数是bit数
        break;
    }

    case IR_PROTOCOL_AEHA:
    {
        uint16_t customer_code;
        uint16_t data_bit_count;
        uint16_t data_byte_count;
        uint16_t raw_bit_count;
        uint16_t raw_byte_count;
        if (frame->payload_size < 6U)
        {
            gb_protocol_send_error(frame, ERR_INVALID_PAYLOAD, 0U);
            return;
        }
        customer_code = (uint16_t)frame->payload[1] | ((uint16_t)frame->payload[2] << 8U);
        data_bit_count = (uint16_t)frame->payload[3] | ((uint16_t)frame->payload[4] << 8U);
        /*
         * 当前接收代码支持的AEHA总长度为48~1280bit。
         * 减去Customer Code 16bit和Parity 4bit，
         * Data Bit Count范围为28~1260bit。
         */
        if ((data_bit_count < 28U) || (data_bit_count > 1260U))
        {
            gb_protocol_send_error(frame, ERR_INVALID_PARAM, 0U);
            return;
        }
        data_byte_count = (uint16_t)((data_bit_count + 7U) / 8U);

        if (frame->payload_size !=(uint16_t)(6U + data_byte_count))
        {
            gb_protocol_send_error(frame,ERR_INVALID_PAYLOAD,0U);
            return;
        }
        
        repeat_count = frame->payload[5U + data_byte_count];

        if (repeat_count == 0U)
        {
            gb_protocol_send_error(frame, ERR_INVALID_PARAM, 0U);
            return;
        }
        /*
         * 实际红外数据：
         * Customer Code 16bit + Parity 4bit + Data
         */
        raw_bit_count = (uint16_t)(20U + data_bit_count);
        raw_byte_count = (uint16_t)((raw_bit_count + 7U) / 8U);
        if (raw_byte_count > sizeof(g_ir_tx.ir_tx_data))
        {
            gb_protocol_send_error(frame, ERR_INVALID_PARAM, 0U);
            return;
        }
        memset(g_ir_tx.ir_tx_data, 0, raw_byte_count);
        g_ir_tx.ir_tx_data[0] = (uint8_t)(customer_code & 0xFFU);

        g_ir_tx.ir_tx_data[1] = (uint8_t)(customer_code >> 8U);

        uint16_t parity = gbe_ir_aeha_customer_parity(customer_code);

        g_ir_tx.ir_tx_data[2] = parity;
        for (uint16_t i = 0U; i < data_byte_count; i++)
        {
            g_ir_tx.ir_tx_data[2U + i] |= (uint8_t)(frame->payload[5U + i] << 4U);

            if ((3U + i) < raw_byte_count)
            {
                g_ir_tx.ir_tx_data[3U + i] |= (uint8_t)(frame->payload[5U + i] >> 4U);
            }
        }
        g_ir_tx.sequence = frame->sequence;

        IR_SendData(IR_PROTOCOL_AEHA, g_ir_tx.ir_tx_data, raw_bit_count, repeat_count);
        /* code */
        break;
    }

    case IR_PROTOCOL_SONY:
    {
        /* code */
        if (frame->payload_size != 6U)
        {
            gb_protocol_send_error(frame, ERR_INVALID_PAYLOAD, 0U);
            return;
        }
        /*
         * 检查Data高位为0
         * 检查Bit Count是12/15/20
         * 检查Address未使用高位为0
         * 检查Repeat Count不为0
         */

        // sony发射是先发射7位data在发剩下的addr
        uint16_t addr = (uint16_t)(frame->payload[1] | (frame->payload[2] << 8));
        uint8_t data = frame->payload[3];
        uint8_t bit_count = frame->payload[4];
        repeat_count = frame->payload[5];
        uint16_t address_mask;
        uint32_t raw_data;
        switch (bit_count)
        {
        case 12U:
            address_mask = 0x001FU; /* 5bit Address */
            break;

        case 15U:
            address_mask = 0x00FFU; /* 8bit Address */
            break;

        case 20U:
            address_mask = 0x1FFFU; /* 13bit Address */
            break;

        default:
            gb_protocol_send_error(frame, ERR_INVALID_PARAM, 0U);
            return;
        }

        /* 检查Address未使用的高位是否为0 */
        if ((addr & (uint16_t)(~address_mask)) != 0U)
        {
            gb_protocol_send_error(frame, ERR_INVALID_PARAM, 0U);
            return;
        }

        raw_data = (uint32_t)(data & 0x7FU);
        raw_data |= ((uint32_t)addr << 7U);
        g_ir_tx.ir_tx_data[0] = (uint8_t)(raw_data & 0xFF); // sony先发送7位data
        g_ir_tx.ir_tx_data[1] = (uint8_t)((raw_data >> 8) & 0xFFU);
        g_ir_tx.ir_tx_data[2] = (uint8_t)((raw_data >> 16) & 0xFFU);
        g_ir_tx.sequence = frame->sequence;
        IR_SendData(IR_PROTOCOL_SONY, g_ir_tx.ir_tx_data, bit_count, repeat_count);

        break;
    }

    default:
        gb_protocol_send_error(frame, ERR_INVALID_PARAM, 0U);
        return;
    }

    if (!IR_IsSending())
    {
        gb_protocol_send_error(frame, ERR_INTERNAL, 0U);
        return;
    }
    g_ir_tx.pending = 1; // 参数有效且发送已启动，保留请求直到响应入队
}

static gb_request_callback_t gb_callback[2] = {NULL};
uint8_t payload[164] = {0};
static void gbe_protocol_ir_receive(const IR_ReceiveEvent_t *event)
{
    uint8_t payload_size = 0;

    switch (event->protocol)
    {
    case IR_PROTOCOL_NEC:
    {
        /* code */
        payload[0] = IR_PROTOCOL_NEC;
        payload[1] = event->data[0];
        payload[2] = event->data[1];
        payload[3] = event->data[2];
        payload[4] = event->repeat_count;
        payload_size = 5;
        break;
    }

    case IR_PROTOCOL_AEHA:
    {
        /* code */

        if ((event->bit_count < 48U) ||
            (event->bit_count > 1280U))
        {
            return;
        }
        // 因为AEHA这个总位数是包括用户码的，所以需要减去16位，还要减去检验码4位
        uint16_t data_bit_count = event->bit_count - 16 - 4;
        uint16_t data_byte_count = (data_bit_count + 7) / 8; // 有效数据的个数
        uint16_t raw_byte_count = (event->bit_count + 7U) / 8U;
        payload[0] = (uint8_t)IR_PROTOCOL_AEHA;
        payload[1] = event->data[0];
        payload[2] = event->data[1];
        payload[3] = (uint8_t)(data_bit_count & 0xFFU);
        payload[4] = (uint8_t)(data_bit_count >> 8);

        for (uint16_t i = 0; i < data_byte_count; i++)
        {
            payload[5U + i] = (uint8_t)(event->data[2U + i] >> 4);
            if ((3U + i) < raw_byte_count)
            {
                payload[5U + i] |= (uint8_t)(event->data[3U + i] << 4);
            }
        }

        // memcpy(&payload[5], &event->data[2], data_byte_count);
        payload[5 + data_byte_count] = event->repeat_count;
        payload_size = 6 + data_byte_count;
        break;
    }

    case IR_PROTOCOL_SONY:
    {
        /* code */
        if (event->bit_count != 12 && event->bit_count != 15 && event->bit_count != 20)
        {
            return;
        }
        uint16_t address = 0;
        uint8_t data = 0;
        uint32_t raw = 0;
        raw = (uint32_t)event->data[0];
        raw |= (uint32_t)event->data[1] << 8;
        raw |= (uint32_t)event->data[2] << 16;

        data = (uint8_t)(raw & 0x7F);
        address = (uint16_t)(raw >> 7);

        if (event->bit_count == 12)
        {
            address &= 0x1F;
        }
        else if (event->bit_count == 15)
        {
            address &= 0xFF;
        }
        else if (event->bit_count == 20)
        {
            address &= 0x1FFF;
        }

        // sony是先发送低7位数据在发送地址
        payload[0] = (uint8_t)IR_PROTOCOL_SONY;
        payload[1] = (uint8_t)(address & 0xFFU);
        payload[2] = (uint8_t)(address >> 8);
        payload[3] = data;
        payload[4] = (uint8_t)event->bit_count;
        payload[5] = event->repeat_count;
        payload_size = 6U;

        break;
    }

    default:
    {
        return;
    }
    }

    gb_protocol_send_notification(CMD_IR_RECEIVE_NOTIFY, payload, payload_size);
}

void gbe_protocol_init(void)
{
    gb_protocol_init();

    // 注册回调给下层gb.c使用,因为下层不能直接调用上层函数，防止重复依赖
    gb_callback[0] = gbe_protocol_pc_request_motion;
    gb_callback[1] = gbe_protocol_pc_request_ir_tansimit;
    gb_protocol_register_callback(gb_callback, 2);

    IR_RegisterReceiveCallback(gbe_protocol_ir_receive);
}

// 每500ms上传测光数据
void gbe_protocol_upload_ambient_light(void)
{
    uint32_t illuminance;
    uint16_t current_time = 0;
    static uint16_t last_time = 0;
    float lux = 0;
    uint8_t ret;
    uint8_t payload[5];

    // 可以通过TIM7_GetMs()获取当前时间，然后与上次上传时间进行比较，如果超过500ms则上传数据
    current_time = TIM7_GetMs();
    if ((uint16_t)(current_time - last_time) < GBE_ALS_UPLOAD_INTERVAL_MS)
    {
        return;
    }
    last_time = current_time;

    ret = LTR329_CalculateLux(LTR329_GAIN_8X, LTR329_INT_100MS, 1.0, &lux);
    if (ret == LTR329_ERR_NO_NEW_DATA)
    {
        // 没有更新数据
        return;
    }
    if (ret != LTR329_OK || lux < 0 || lux > GBE_ALS_MAX_LUX)
    {
        printf("LTR329_CalculateLux error: %d\n", ret);
        // Status = Sensor Error，照度必须为0
        payload[0] = 1U;
        illuminance = 0U;
    }
    else
    {
        payload[0] = 0;
        // 单位转换为0.1 lx，并四舍五入
        illuminance = (uint32_t)(lux * 10.0f + 0.5f);
    }
    printf("illuminance: %d\n", illuminance);
    // uint32小端拼接，不能直接使用结构体
    payload[1] = (uint8_t)(illuminance & 0xFFU);
    payload[2] = (uint8_t)((illuminance >> 8) & 0xFFU);
    payload[3] = (uint8_t)((illuminance >> 16) & 0xFFU);
    payload[4] = (uint8_t)((illuminance >> 24) & 0xFFU);

    // 拿到lux,开始组帧，发送notify帧
    gb_protocol_send_notification(CMD_AMBIENT_LIGHT_NOTIFY, payload, 5);
}

// extern volatile uint8_t g_pir_changed_flags;
static void gbe_protocol_upload_motion(void)
{
    // if(g_pir_changed_flags == 0)
    // {
    //     return;
    // }
    uint8_t payload[3] = {0};
    // if((g_pir_changed_flags & PIR_CHANGED_LEFT) && (g_pir_changed_flags & PIR_CHANGED_RIGHT))
    // {
    //     //这里就发
    // }
    // payload[0] = 0;
    PIR_GetStates(&payload[1], &payload[2]);

    if (!PIR_TakeEvent(&payload[0], &payload[1], &payload[2]))
    {
        // printf("no event\r\n");
        return;
    }

    gb_protocol_send_notification(CMD_MOTION_NOTIFY, payload, 3);
}

static bool gbe_protocol_ir_response_poll(void)
{
    Frame_t request = {0};
    bool sent;

    if((g_ir_tx.pending == 0) || IR_IsSending())
    {
        return true;
    }
    /* 保存的序号属于当前待响应请求，不依赖原接收缓冲区。 */
    request.command = CMD_IR_SEND_REQ;
    request.sequence = g_ir_tx.sequence;
    if(IR_Transimit_complete())
    {
        /* send_response会将请求命令0x0402转换为响应0x1402 */
        sent = gb_protocol_send_response(&request, NULL, 0U);
    }
    else
    {
        sent = gb_protocol_send_error(&request, ERR_INTERNAL, 0U);
    }

    if (sent)
    {
        g_ir_tx.pending = 0;
    }
    return sent; // 入队失败时保留pending，下轮重试
}

void gbe_protocol_poll(void)
{
    if (!gbe_protocol_ir_response_poll())
    {
        return; // 优先提交红外完成响应
    }
    gbe_protocol_upload_ambient_light();
    gbe_protocol_upload_motion();
}
