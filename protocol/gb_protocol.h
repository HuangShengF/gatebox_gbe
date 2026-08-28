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
#define CMD_HANDSHAKE_REQ       0x0001
#define CMD_HANDSHAKE_RESP      0x1001
#define CMD_HANDSHAKE_ERR       0x3001

// ============ 错误码 ============
#define ERR_UNSUPPORTED_CMD     0x0001
#define ERR_INVALID_PAYLOAD     0x0002
#define ERR_INVALID_PARAM       0x0003
#define ERR_INVALID_STATE       0x0004
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


uint16_t gb_protocol_crc16(const uint8_t *data, uint16_t len);
void gb_protocol_process_byte(uint8_t byte);
void gb_protocol_init(void);
uint16_t TIM7_GetMs(void);
#endif
