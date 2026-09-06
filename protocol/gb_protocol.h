#ifndef __GB_PROTOCOL_H
#define __GB_PROTOCOL_H
#include "n32l40x.h"
#include "log.h"

// ============ 协议常量 ============
#define PROTOCOL_MAGIC          0xB3A5
#define PROTOCOL_HEADER_SIZE    8
#define PROTOCOL_MAX_PAYLOAD    2048
#define PROTOCOL_MIN_FRAME      10

// ============ 命令定义 ============
// 握手
#define CMD_HANDSHAKE_REQ       0x0001
#define CMD_HANDSHAKE_RESP      0x1001
#define CMD_HANDSHAKE_ERR       0x3001

// motion状态查询
#define CMD_MOTION_REQ        0x0303
#define CMD_MOTION_RESP       0x1303

// IR红外发送：PC->MCU
#define CMD_IR_SEND_REQ        0x0402
#define CMD_IR_SEND_RESP       0x1402
#define CMD_IR_SEND_ERR        0x3402

// 环境光通知：MCU -> PC
#define CMD_AMBIENT_LIGHT_NOTIFY    0x2301
#define CMD_MOTION_NOTIFY           0x2302

// ============ 错误码 ============
#define ERR_UNSUPPORTED_CMD     0x0001
#define ERR_INVALID_PAYLOAD     0x0002
#define ERR_INVALID_PARAM       0x0003
#define ERR_INVALID_STATE       0x0004
#define ERR_INTERNAL            0x0006
#define ERR_PROTOCOL_VERSION    0x0007

// ============ 超时时间 ============
#define PROTOCOL_INTER_BYTE_TIMEOUT_MS  100U
#define PROTOCOL_FRAME_TIMEOUT_MS       500U

// ============ 帧结构 ============
typedef struct {
    uint16_t magic;
    uint16_t sequence;
    uint16_t command;
    uint16_t payload_size;
    const uint8_t *payload;
    uint16_t crc16;
} Frame_t;

typedef void (*gb_request_callback_t)(const Frame_t *frame);


// ============ 会话状态 ============
typedef enum {
    SESSION_AWAITING_HANDSHAKE = 0,
    SESSION_ESTABLISHED = 1
} SessionState_t;

// ============ 接收状态机 ============
typedef enum {
    RX_STATE_IDLE = 0,
    RX_STATE_HEADER,
    RX_STATE_PAYLOAD,
    RX_STATE_CRC,
    RX_STATE_COMPLETE
} RxState_t;

typedef struct
{
    // 超时
    uint16_t last_byte_time;
    uint16_t last_frame_time;

    SessionState_t session_state;
    uint16_t notification_seq;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    // 临时buf
    uint8_t rx_buf[PROTOCOL_MAX_PAYLOAD + PROTOCOL_HEADER_SIZE + 2];
    uint16_t index;
    uint16_t rx_payload_size;
    Frame_t rx_frame;
    RxState_t state;
} gb_protocol_rx_t;



bool gb_protocol_send_notification(uint16_t command,const uint8_t *payload, uint16_t payload_len);
uint16_t gb_protocol_crc16(const uint8_t *data, uint16_t len);
void gb_protocol_process_byte(uint8_t byte);
void gb_protocol_init(void);
uint16_t TIM7_GetMs(void);
bool Protocol_SendFrame(uint16_t command, uint16_t sequence, const uint8_t *payload, uint16_t payload_len);
void gb_protocol_register_callback(gb_request_callback_t *callback, uint8_t num);
bool gb_protocol_send_response(const Frame_t *request, const uint8_t *payload, uint16_t payload_len);
bool gb_protocol_send_error(const Frame_t *request, uint16_t error_code, uint16_t detail);
#endif
