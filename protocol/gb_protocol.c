#include "gb_protocol.h"
#include "usb_cdc.h"
#include "stdlib.h"
#include "string.h"
static gb_protocol_rx_t g_ctx;

#define MOTION_CALLBACK_ID 0U
#define IR_SEND_CALLBACK_ID 1U
#define CALLBACK_COUNT 2U
static gb_request_callback_t gbe_callback[CALLBACK_COUNT] = {NULL};
void TIM7_Configuration(void);

void gb_protocol_register_callback(gb_request_callback_t *callback, uint8_t num)
{
    if ((num > 2) || (callback == NULL))
    {
        printf("callback num or callback invalid/r/n");
        return;
    }
    for (int i = 0; i < num; i++)
    {
        gbe_callback[i] = callback[i];
    }
}

uint16_t gb_protocol_crc16(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0))
    {
        return 0;
    }
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// 协议初始化
void gb_protocol_init(void)
{
    TIM7_Configuration();
    g_ctx.state = RX_STATE_IDLE;
    g_ctx.index = 0;
    g_ctx.rx_payload_size = 0;
    g_ctx.session_state = SESSION_AWAITING_HANDSHAKE;
    g_ctx.notification_seq = 1;
    g_ctx.protocol_major = 1;
    g_ctx.protocol_minor = 0;
    g_ctx.last_byte_time = 0;
    g_ctx.last_frame_time = 0;
}

// ============ 发送帧 ============
bool Protocol_SendFrame(uint16_t command, uint16_t sequence, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t tx_buf[500] = {0};
    // 组帧
    // 构建帧头
    tx_buf[0] = PROTOCOL_MAGIC & 0xFF;
    tx_buf[1] = (PROTOCOL_MAGIC >> 8) & 0xFF;
    tx_buf[2] = sequence & 0xFF;
    tx_buf[3] = (sequence >> 8) & 0xFF;
    tx_buf[4] = command & 0xFF;
    tx_buf[5] = (command >> 8) & 0xFF;
    tx_buf[6] = payload_len & 0xFF;
    tx_buf[7] = (payload_len >> 8) & 0xFF;
    // payload
    if (payload_len != 0 && payload != NULL)
        memcpy(&tx_buf[8], payload, payload_len);

    // 计算CRC
    uint16_t crc;
    crc = gb_protocol_crc16(tx_buf, 8 + payload_len);
    tx_buf[8 + payload_len] = (uint8_t)(crc & 0xFF);
    tx_buf[8 + payload_len + 1] = (uint8_t)(crc >> 8);
    // 调用USB发送函数
    return USB_CDC_Write(tx_buf, 8 + payload_len + 2);
    // USB_SilWrite(EP1_IN, tx_buf, total_len);
}
bool gb_protocol_send_notification(uint16_t command, const uint8_t *payload, uint16_t payload_len)
{
    uint16_t sequence;

    if ((payload_len != 0U) && (payload == NULL))
    {
        return false;
    }

    // 未完成握手，不允许发送普通Notification
    if (g_ctx.session_state != SESSION_ESTABLISHED)
    {
        return false;
    }

    sequence = g_ctx.notification_seq;
    bool ret = Protocol_SendFrame(command, sequence, payload, payload_len);
    if(!ret)
    {
        printf("[GB] Send Notification Failed\n");
        // 如果通知不成功，sequence不增加
        return false;
    }


    g_ctx.notification_seq++;
    if (g_ctx.notification_seq == 0U)
    {
        printf("[GB] Notification Sequence Overflow\n");
        // 0保留，0xFFFF之后回到1
        g_ctx.notification_seq = 1U;
    }

    return true;
}

bool gb_protocol_send_response(const Frame_t *request, const uint8_t *payload, uint16_t payload_len)
{
    uint16_t response_cmd;

    if ((request == NULL) || ((payload_len != 0U) && (payload == NULL)))
    {
        return false;
    }

    response_cmd = (request->command & 0x0FFFU) | 0x1000U;
    return Protocol_SendFrame(response_cmd, request->sequence, payload, payload_len);
}

bool gb_protocol_send_error(const Frame_t *request, uint16_t error_code, uint16_t detail)
{
    uint8_t err_payload[4];
    uint16_t error_cmd;

    if (request == NULL)
    {
        return false;
    }

    err_payload[0] = (uint8_t)(error_code & 0xFFU);
    err_payload[1] = (uint8_t)((error_code >> 8) & 0xFFU);
    err_payload[2] = (uint8_t)(detail & 0xFFU);
    err_payload[3] = (uint8_t)((detail >> 8) & 0xFFU);
    error_cmd = (request->command & 0x0FFFU) | 0x3000U;

    return Protocol_SendFrame(error_cmd, request->sequence, err_payload, sizeof(err_payload));
}

// ============ 处理握手请求 ============
static void Handle_Handshake(const Frame_t *frame)
{

    // 已握手在次Handshake,建立新的会话
    g_ctx.session_state = SESSION_AWAITING_HANDSHAKE;

    if (frame->payload_size != 8)
    {
        // 不是8字节
        uint8_t err_payload[4];
        err_payload[0] = ERR_INVALID_PAYLOAD & 0xFF;
        err_payload[1] = (ERR_INVALID_PAYLOAD >> 8) & 0xFF;
        err_payload[2] = 8; // Expected Value = 8
        err_payload[3] = 0;
        Protocol_SendFrame(CMD_HANDSHAKE_ERR, frame->sequence, err_payload, 4);
        return;
    }
    // 解析请求
    uint8_t host_major = frame->payload[0];
    uint8_t host_minor = frame->payload[1];
    uint16_t host_max_payload = frame->payload[2] | (frame->payload[3] << 8);
    printf("host vision: %d.%d, host_max_payload:%d\n", host_major, host_minor, host_max_payload);

    // 检查版本
    if (host_major != 1)
    {
        // 版本不匹配
        uint8_t err_payload[4];
        err_payload[0] = ERR_PROTOCOL_VERSION & 0xFF;
        err_payload[1] = (ERR_PROTOCOL_VERSION >> 8) & 0xFF;
        err_payload[2] = 1; // Device Major Version
        err_payload[3] = 0;
        Protocol_SendFrame(CMD_HANDSHAKE_ERR, frame->sequence, err_payload, 4);
        return;
    }

    // 协商版本（取较小的 Minor）
    g_ctx.protocol_major = 1;
    g_ctx.protocol_minor = 0;

    // 构建响应 Payload
    uint8_t resp[58];
    memset(resp, 0, sizeof(resp));

    resp[0] = g_ctx.protocol_major; // Protocol Major
    resp[1] = g_ctx.protocol_minor; // Protocol Minor
    resp[2] = 2;                    // Device Model = 2 (GBE3)
    resp[3] = 0;                    // Reserved

    uint16_t device_max = PROTOCOL_MAX_PAYLOAD;
    resp[4] = device_max & 0xFF;
    resp[5] = (device_max >> 8) & 0xFF;

    // Device Capability Flags (4 bytes, all 0)
    resp[6] = 0;
    resp[7] = 0;
    resp[8] = 0;
    resp[9] = 0;

    // Hardware Version (16 bytes)
    const char *hw_ver = "HW_1.0";
    memcpy(&resp[10], hw_ver, strlen(hw_ver));

    // Firmware Version (16 bytes)
    const char *fw_ver = "FW_1.0.0";
    memcpy(&resp[26], fw_ver, strlen(fw_ver));

    // Serial Number (16 bytes)
    const char *sn = "GBE3-00000001";
    memcpy(&resp[42], sn, strlen(sn));

    // 发送响应
    bool ret = Protocol_SendFrame(CMD_HANDSHAKE_RESP, frame->sequence, resp, 58);
    if (ret)
    {
        // 切换到已建立状态
        g_ctx.session_state = SESSION_ESTABLISHED;
        g_ctx.notification_seq = 1; // 重置通知序列号
    }
}

// ============ 处理接收到的完整帧 ============
static void Handle_Frame(const Frame_t *frame)
{

    // 根据command 处理:这些都是主机发来的request
    switch (frame->command)
    {
    case CMD_HANDSHAKE_REQ:
        // PC端发送的握手帧，需要给它回应
        Handle_Handshake(frame);
        break;
    case CMD_MOTION_REQ:
        // 查询运动传感器状态,使用回调
        if(gbe_callback[MOTION_CALLBACK_ID] == NULL) return;
       
        gbe_callback[MOTION_CALLBACK_ID](frame);
        break;
    case CMD_IR_SEND_REQ:
        // 使用回调
        if(gbe_callback[IR_SEND_CALLBACK_ID] == NULL) return;
        gbe_callback[IR_SEND_CALLBACK_ID](frame);
        break;
    default:
        // 未知命令
        if ((frame->command & 0xF000) == 0x0000)
        { // 是请求
            uint8_t err_payload[4];
            err_payload[0] = ERR_UNSUPPORTED_CMD & 0xFF;
            err_payload[1] = (ERR_UNSUPPORTED_CMD >> 8) & 0xFF;
            err_payload[2] = 0;
            err_payload[3] = 0;
            uint16_t err_cmd = (frame->command & 0x0FFF) | 0x3000;
            Protocol_SendFrame(err_cmd, frame->sequence, err_payload, 4);
        }
        break;
    }
}

// 接受状态机
void gb_protocol_process_byte(uint8_t byte)
{
    uint16_t current_time = TIM7_GetMs();
    // 字节间超时
    if ((uint16_t)(current_time - g_ctx.last_byte_time) > PROTOCOL_INTER_BYTE_TIMEOUT_MS)
    {
        g_ctx.index = 0;
        g_ctx.state = RX_STATE_IDLE;
    }
    g_ctx.last_byte_time = current_time;

    // 帧完成超时，也就是一帧必须在500ms内完成
    if ((uint16_t)(current_time - g_ctx.last_frame_time) > PROTOCOL_FRAME_TIMEOUT_MS)
    {
        g_ctx.index = 0;
        g_ctx.state = RX_STATE_IDLE;
    }

    switch (g_ctx.state)
    {
    case RX_STATE_IDLE:
        // 判断帧头
        if (g_ctx.index == 0 && byte == (PROTOCOL_MAGIC & 0xFF))
        {
            g_ctx.rx_buf[g_ctx.index++] = byte;
            // 帧开始时间
            g_ctx.last_frame_time = current_time;
        }
        else if (g_ctx.index == 1 && byte == (PROTOCOL_MAGIC >> 8))
        {
            g_ctx.rx_buf[g_ctx.index++] = byte;
            g_ctx.state = RX_STATE_HEADER;
        }
        else if (g_ctx.index == 1 && byte == (PROTOCOL_MAGIC & 0xFF))
        {
            /* A5 A5：保留最后一个A5作为新Magic起点 */
            g_ctx.rx_buf[0] = byte;
            g_ctx.index = 1;
            // 帧开始时间
            g_ctx.last_frame_time = current_time;
        }
        else
        {
            g_ctx.index = 0;
        }
        break;
    case RX_STATE_HEADER:
        g_ctx.rx_buf[g_ctx.index++] = byte;
        if (g_ctx.index >= PROTOCOL_HEADER_SIZE)
        {
            // 帧头接受完成，算payload size是否大于2048
            g_ctx.rx_payload_size = g_ctx.rx_buf[6] | (g_ctx.rx_buf[7] << 8);
            if (g_ctx.rx_payload_size > PROTOCOL_MAX_PAYLOAD)
            {
                g_ctx.state = RX_STATE_IDLE;
                g_ctx.index = 0;
            }
            else if (g_ctx.rx_payload_size == 0)
            {
                // 帧头接受完成，payload size为0，直接接收CRC
                g_ctx.state = RX_STATE_CRC;
            }
            else
            {
                g_ctx.state = RX_STATE_PAYLOAD;
            }
        }
        break;
    case RX_STATE_PAYLOAD:
        g_ctx.rx_buf[g_ctx.index++] = byte;
        // 就一直收payload
        if (g_ctx.index >= g_ctx.rx_payload_size + PROTOCOL_HEADER_SIZE)
        {
            g_ctx.state = RX_STATE_CRC;
        }

        break;
    case RX_STATE_CRC:
        // 接受payload完成，校验CRC
        g_ctx.rx_buf[g_ctx.index++] = byte;
        if (g_ctx.index >= PROTOCOL_HEADER_SIZE + g_ctx.rx_payload_size + 2)
        {
            // CRC 接收完成，校验
            uint16_t calc_crc = gb_protocol_crc16(g_ctx.rx_buf,
                                                  PROTOCOL_HEADER_SIZE + g_ctx.rx_payload_size);
            uint16_t recv_crc = g_ctx.rx_buf[PROTOCOL_HEADER_SIZE + g_ctx.rx_payload_size] |
                                (g_ctx.rx_buf[PROTOCOL_HEADER_SIZE + g_ctx.rx_payload_size + 1] << 8);

            if (calc_crc == recv_crc)
            {
                // CRC 正确，解析帧
                g_ctx.rx_frame.magic = g_ctx.rx_buf[0] | (g_ctx.rx_buf[1] << 8);
                g_ctx.rx_frame.sequence = g_ctx.rx_buf[2] | (g_ctx.rx_buf[3] << 8);
                g_ctx.rx_frame.command = g_ctx.rx_buf[4] | (g_ctx.rx_buf[5] << 8);
                g_ctx.rx_frame.payload_size = g_ctx.rx_payload_size;
                // memcpy(g_ctx.rx_frame.payload, &g_ctx.rx_buf[8], g_ctx.rx_payload_size);
                g_ctx.rx_frame.payload = &g_ctx.rx_buf[8];
                g_ctx.rx_frame.crc16 = recv_crc;

                // 未握手时，非Handshake Request返回Invalid State
                if ((g_ctx.session_state != SESSION_ESTABLISHED) && (g_ctx.rx_frame.command != CMD_HANDSHAKE_REQ) &&
                    (g_ctx.rx_frame.command & 0xF000) == 0x0000)
                {
                    // 只回复主机非Handshake Request，对Response/Error 不会回
                    uint8_t err_payload[4];
                    err_payload[0] = ERR_INVALID_STATE & 0xFF;
                    err_payload[1] = (ERR_INVALID_STATE >> 8) & 0xFF;
                    err_payload[2] = 0;
                    err_payload[3] = 0;
                    uint16_t err_cmd = (g_ctx.rx_frame.command & 0x0FFF) | 0x3000;
                    Protocol_SendFrame(err_cmd, g_ctx.rx_frame.sequence, err_payload, 4);
                    // 重置状态机
                    g_ctx.state = RX_STATE_IDLE;
                    g_ctx.index = 0;
                    return;
                }

                // 处理帧
                Handle_Frame(&g_ctx.rx_frame);
            }

            // 重置状态机
            g_ctx.state = RX_STATE_IDLE;
            g_ctx.index = 0;
        }
        break;

    default:
        // 重置状态机
        g_ctx.state = RX_STATE_IDLE;
        g_ctx.index = 0;
        break;
    }
}

void TIM7_Configuration(void)
{
    TIM_TimeBaseInitType TIM_TimeBaseStructure;
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM7, ENABLE);
    /* Time base configuration */
    TIM_InitTimBaseStruct(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.Period = 65535;
    TIM_TimeBaseStructure.Prescaler = 24000 - 1;
    TIM_TimeBaseStructure.ClkDiv = 0;
    TIM_TimeBaseStructure.CntMode = TIM_CNT_MODE_UP;

    TIM_InitTimeBase(TIM7, &TIM_TimeBaseStructure);
    TIM_SetCnt(TIM7, 0U);
    /* TIM7 enable counter */
    TIM_Enable(TIM7, ENABLE);
}
uint16_t TIM7_GetMs(void)
{
    return TIM_GetCnt(TIM7);
}
