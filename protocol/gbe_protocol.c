#include "gbe_protocol.h"
#include "gb_protocol.h"
#include "bsp_pir.h"
#include "bsp_ltr329.h"
#include "log.h"
#include "bsp_ir.h"
#include <string.h>
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

static void gbe_protocol_pc_request_ir_tansimit(const Frame_t *frame)
{
    // PC发来的数据进行解码然后进行发射
    uint8_t format = frame->payload[0];
    switch (format)
    {
    case IR_PROTOCOL_NEC:
    {
        /* code */
        uint8_t nec_buf[4] = {0};
        nec_buf[0] = frame->payload[1];
        nec_buf[1] = frame->payload[2];
        nec_buf[2] = frame->payload[3];
        nec_buf[3] = ~(frame->payload[4]); 
        uint8_t repeat_count = frame->payload[5];
        // IR_SendData(IR_PROTOCOL_NEC, nec_buf, 4);

        break;
    }

    case IR_PROTOCOL_AEHA:
    {

        /* code */
        break;
    }

    case IR_PROTOCOL_SONY:
    {
        /* code */
        // 地址16位，data8位
        uint8_t sony_buf[3] = {0};
        break;
    }

    default:
        break;
    }
}

static gb_request_callback_t gb_callback[2] = {NULL};
uint8_t payload[164] = {0};
static void gbe_protocol_ir_receive(const IR_ReceiveEvent_t *event)
{ 
   uint8_t payload_size = 0;

    switch(event->protocol)
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
            // 因为AEHA这个总位数是包括用户码的，所以需要减去16位
            uint16_t data_bit_count = event->bit_count - 16;
            uint16_t data_byte_count = (data_bit_count + 7) / 8; //有效数据的个数
            payload[0] = (uint8_t)IR_PROTOCOL_AEHA;
            payload[1] = event->data[0];
            payload[2] = event->data[1];
            payload[3] = (uint8_t)(data_bit_count & 0xFFU);
            payload[4] = (uint8_t)(data_bit_count >> 8);
            memcpy(&payload[5], &event->data[2], data_byte_count);
            payload[5 + data_byte_count] = event->repeat_count;
            payload_size = 6 + data_byte_count;
            break;
        }
       

        case IR_PROTOCOL_SONY:
        {
  /* code */
            if(event->bit_count != 12 && event->bit_count != 15 && event->bit_count != 20)
            {
                return;
            }
            uint16_t address = 0;
            uint8_t data = 0;
            uint32_t raw = 0;
            raw  = (uint32_t)event->data[0];
            raw |= (uint32_t)event->data[1] << 8;
            raw |= (uint32_t)event->data[2] << 16;

            data = (uint8_t)(raw & 0x7F);
            address = (uint16_t)(raw >> 7);

            if(event->bit_count == 12)
            {
                address &= 0x1F;
            }
            else if(event->bit_count == 15)
            {
                address &= 0xFF;
            }
            else if(event->bit_count == 20)
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
    if(ret == LTR329_ERR_NO_NEW_DATA)
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

    if(!PIR_TakeEvent(&payload[0],&payload[1],&payload[2]))
    {
        // printf("no event\r\n");
        return;
    }

    gb_protocol_send_notification(CMD_MOTION_NOTIFY, payload, 3);
}

void gbe_protocol_poll(void)
{
    gbe_protocol_upload_ambient_light();
    // gbe_protocol_upload_motion();
}