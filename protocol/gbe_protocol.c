#include "gbe_protocol.h"
#include "gb_protocol.h"
#include "bsp_pir.h"
#include "bsp_ltr329.h"

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
}

static gb_request_callback_t gb_callback[2] = {NULL};

void gbe_protocol_init(void)
{
    gb_protocol_init();

    // 注册回调给下层gb.c使用,因为下层不能直接调用上层函数，防止重复依赖
    gb_callback[0] = gbe_protocol_pc_request_motion;
    gb_callback[1] = gbe_protocol_pc_request_ir_tansimit;
    gb_protocol_register_callback(gb_callback, 2);
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
    if ((uint16_t)current_time - last_time < GBE_ALS_UPLOAD_INTERVAL_MS)
    {
        return;
    }
    last_time = current_time;

    ret = LTR329_CalculateLux(LTR329_GAIN_1X, LTR329_INT_100MS, 1.0, &lux);
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

    // uint32小端拼接，不能直接使用结构体
    payload[1] = (uint8_t)(illuminance & 0xFFU);
    payload[2] = (uint8_t)((illuminance >> 8) & 0xFFU);
    payload[3] = (uint8_t)((illuminance >> 16) & 0xFFU);
    payload[4] = (uint8_t)((illuminance >> 24) & 0xFFU);

    // 拿到lux,开始组帧，发送notify帧
    gb_protocol_send_notification(CMD_AMBIENT_LIGHT_NOTIFY, payload, 5);
}

void gbe_protocol_poll(void)
{
    gbe_protocol_upload_ambient_light();
}