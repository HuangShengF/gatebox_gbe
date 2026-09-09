/* Hardware/USB mocks only; the runner inserts the current production functions. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint32_t AR;
    uint32_t CCDAT3;
    uint8_t enabled;
    uint8_t pending;
    uint8_t output;
} TestTimer;
static TestTimer timer2, timer6, timer7;
#define TIM2 (&timer2)
#define TIM6 (&timer6)
#define TIM7 (&timer7)
#define ENABLE 1
#define DISABLE 0
#define RESET 0
#define TIM_CH_3 3
#define TIM_CAP_CMP_ENABLE 1
#define TIM_CAP_CMP_DISABLE 0
#define TIM_INT_UPDATE 1
#define TIM6_IRQn 6

static uint32_t now_us, next_irq_us;
static unsigned frame_count, response_count, error_count, checks;
static uint32_t starts[256];
static unsigned marks[256], repeats[256];
static uint8_t usb_ready = 1;
static uint16_t last_sequence, last_error;

static void TIM_Enable(TestTimer *timer, int enable);
static void TIM_SetCnt(TestTimer *timer, uint16_t count) { (void)timer; (void)count; }
static void TIM_SetAutoReload(TestTimer *timer, uint16_t count) { timer->AR = count; }
static void TIM_ClrIntPendingBit(TestTimer *timer, int bit) { (void)bit; timer->pending = 0; }
static int TIM_GetIntStatus(TestTimer *timer, int bit) { (void)bit; return timer->pending; }
static uint16_t TIM_GetCnt(TestTimer *timer) { (void)timer; return (uint16_t)(now_us / 1000U); }
static void TIM_EnableCapCmpCh(TestTimer *timer, int channel, int enable);
static void NVIC_DisableIRQ(int irq) { (void)irq; }
static void NVIC_EnableIRQ(int irq) { (void)irq; }
static void NVIC_ClearPendingIRQ(int irq) { (void)irq; }

/* SOURCE_TYPES */

static bool gb_protocol_send_response(const Frame_t *frame, const uint8_t *data, uint16_t size);
static bool gb_protocol_send_error(const Frame_t *frame, uint16_t error, uint16_t detail);

/* SOURCE_FUNCTIONS */

#define CHECK(expr) do { checks++; if (!(expr)) { \
    printf("FAIL line %u: %s\n", (unsigned)__LINE__, #expr); exit(1); } } while (0)

static void TIM_Enable(TestTimer *timer, int enable)
{
    timer->enabled = (uint8_t)enable;
    if (timer == TIM6 && enable)
    {
        next_irq_us = now_us + timer->AR + 1U;
    }
}

static void TIM_EnableCapCmpCh(TestTimer *timer, int channel, int enable)
{
    (void)channel;
    timer->output = (uint8_t)enable;
    if (enable)
    {
        if (ir_ctrl.state == IR_STATE_START_MARK)
        {
            CHECK(frame_count < 256U);
            starts[frame_count] = now_us;
            repeats[frame_count] = ir_ctrl.nec_repeat_frame;
            marks[frame_count] = 0;
            frame_count++;
        }
        if (ir_ctrl.state == IR_STATE_DATA_MARK)
        {
            CHECK(frame_count != 0U);
            marks[frame_count - 1U]++;
        }
    }
}

static bool gb_protocol_send_response(const Frame_t *frame, const uint8_t *data, uint16_t size)
{
    CHECK(data == NULL && size == 0U);
    CHECK(frame->command == CMD_IR_SEND_REQ);
    CHECK(IR_IsSending() == 0U && IR_TxSucceeded() != 0U);
    if (!usb_ready) return false;
    last_sequence = frame->sequence;
    response_count++;
    return true;
}

static bool gb_protocol_send_error(const Frame_t *frame, uint16_t error, uint16_t detail)
{
    (void)detail;
    if (!usb_ready) return false;
    last_sequence = frame->sequence;
    last_error = error;
    error_count++;
    return true;
}

static void reset_test(uint32_t start_us)
{
    memset(&ir_ctrl, 0, sizeof(ir_ctrl));
    memset(&g_ir_tx, 0, sizeof(g_ir_tx));
    memset(&timer2, 0, sizeof(timer2));
    memset(&timer6, 0, sizeof(timer6));
    now_us = start_us;
    frame_count = response_count = error_count = 0;
    usb_ready = 1;
}

/* Advance all actual TIM6 state transitions, but do not run the main loop. */
static void advance(uint32_t until_us)
{
    unsigned iterations = 0;
    while (timer6.enabled && next_irq_us <= until_us)
    {
        CHECK(++iterations < 10000U);
        now_us = next_irq_us;
        timer6.pending = 1;
        TIM6_IRQHandler();
    }
    now_us = until_us;
}

static void finish_task(void)
{
    uint32_t deadline = now_us + 600000000U;
    while (IR_IsSending())
    {
        CHECK(now_us < deadline);
        advance(now_us + 1000U);
        IR_TransmitPoll();
        gbe_protocol_ir_transmit_poll();
    }
}

static void request(uint8_t *payload, uint16_t length, uint16_t sequence)
{
    Frame_t frame = {0};
    frame.command = CMD_IR_SEND_REQ;
    frame.payload = payload;
    frame.payload_size = length;
    frame.sequence = sequence;
    gbe_protocol_pc_request_ir_tansimit(&frame);
}

static void test_repeats(IR_Protocol_t protocol, uint16_t bits, uint8_t total, uint32_t start)
{
    uint8_t data[160];
    unsigned i;
    uint32_t period = protocol == IR_PROTOCOL_NEC ? 108000U :
                      protocol == IR_PROTOCOL_SONY ? 45000U : 130000U;
    memset(data, 0xFF, sizeof(data));
    reset_test(start);
    IR_SendData(protocol, data, bits, total);
    CHECK(IR_IsSending() && !IR_TxSucceeded());
    IR_SendData(IR_PROTOCOL_SONY, data, 12, 2); /* Busy does not replace the task. */
    CHECK(ir_ctrl.protocol == protocol && ir_ctrl.repeat_total == total);
    finish_task();
    CHECK(IR_TxSucceeded() && ir_ctrl.repeat_done == total && frame_count == total);
    CHECK(!timer6.enabled && !timer2.output);
    for (i = 0; i < total; i++)
    {
        CHECK(marks[i] == ((protocol == IR_PROTOCOL_NEC && i) ? 0U : bits));
        CHECK(repeats[i] == ((protocol == IR_PROTOCOL_NEC && i) ? 1U : 0U));
        if (i) CHECK(starts[i] - starts[i-1] >= period - 1000U);
    }
}

static void test_long_aeha(void)
{
    uint8_t data[160];
    uint32_t end;
    memset(data, 0xFF, sizeof(data));
    reset_test(65530000U); /* Cross the 16-bit millisecond counter wrap. */
    IR_SendData(IR_PROTOCOL_AEHA, data, 1280, 2);
    while (!ir_ctrl.frame_done) advance(next_irq_us);
    end = now_us;
    IR_TransmitPoll();
    CHECK(IR_IsSending() && frame_count == 1U);
    advance(end + 8000U);
    IR_TransmitPoll();
    CHECK(frame_count == 1U);
    advance(end + 9000U);
    IR_TransmitPoll();
    CHECK(frame_count == 2U && starts[1] - end >= 8000U);
    finish_task();
    CHECK(IR_TxSucceeded());
}

static void test_requests(void)
{
    uint8_t nec[] = {1, 0, 0xFF, 0x1C, 3};
    uint8_t aeha[] = {2, 0x34, 0x12, 28, 0, 0xA5, 0x5A, 0xC3, 0x0D, 1};
    uint8_t sony[] = {3, 0x1B, 0, 0x35, 12, 1};
    const uint8_t expected_nec[] = {0, 0xFF, 0x1C, 0xE3};
    const uint8_t expected_aeha[] = {0x34, 0x12, 0x54, 0xAA, 0x35, 0xDC};
    uint8_t saved[170];
    unsigned errors;

    reset_test(0);
    request(nec, sizeof(nec), 42);
    CHECK(g_ir_tx.pending && !response_count && !memcmp(g_ir_tx.ir_tx_data, expected_nec, 4));
    memcpy(saved, g_ir_tx.ir_tx_data, sizeof(saved));
    request(sony, sizeof(sony), 43);
    CHECK(last_error == ERR_BUSY && g_ir_tx.sequence == 42);
    CHECK(!memcmp(saved, g_ir_tx.ir_tx_data, sizeof(saved)));
    usb_ready = 0;
    finish_task();
    CHECK(g_ir_tx.pending && response_count == 0);
    errors = frame_count;
    usb_ready = 1;
    request(sony, sizeof(sony), 44); /* Still owns buffer while response is pending. */
    CHECK(last_error == ERR_BUSY && frame_count == errors);
    CHECK(gbe_protocol_ir_transmit_poll());
    CHECK(!g_ir_tx.pending && response_count == 1 && last_sequence == 42);
    gbe_protocol_ir_transmit_poll();
    CHECK(response_count == 1);

    request(aeha, sizeof(aeha), 45);
    CHECK(!memcmp(g_ir_tx.ir_tx_data, expected_aeha, 6));
    finish_task();
    CHECK(response_count == 2 && last_sequence == 45);
    request(sony, sizeof(sony), 46);
    CHECK(g_ir_tx.ir_tx_data[0] == 0xB5 && g_ir_tx.ir_tx_data[1] == 0x0D);
    finish_task();
    CHECK(response_count == 3 && last_sequence == 46);

    errors = error_count;
    request(nec, 1, 50);
    CHECK(last_error == ERR_INVALID_PAYLOAD);
    request(aeha, 6, 51);
    CHECK(last_error == ERR_INVALID_PAYLOAD);
    nec[4] = 0;
    request(nec, sizeof(nec), 52);
    CHECK(last_error == ERR_INVALID_PARAM);
    sony[3] = 0x80;
    request(sony, sizeof(sony), 53);
    CHECK(last_error == ERR_INVALID_PARAM);
    sony[3] = 0x35;
    sony[5] = 0;
    request(sony, sizeof(sony), 54);
    CHECK(last_error == ERR_INVALID_PARAM && error_count == errors + 5);
    CHECK(!IR_IsSending() && !g_ir_tx.pending);
}

static void test_timeout(void)
{
    uint8_t nec[] = {1, 0, 0xFF, 0x1C, 1};
    reset_test(65535000U);
    request(nec, sizeof(nec), 60);
    now_us += 3000000U; /* No TIM6 completion interrupt. */
    IR_TransmitPoll();
    CHECK(!IR_IsSending() && !IR_TxSucceeded() && !timer2.output && !timer6.enabled);
    gbe_protocol_ir_transmit_poll();
    CHECK(error_count == 1 && last_error == ERR_INTERNAL && response_count == 0);
    request(nec, sizeof(nec), 61);
    finish_task();
    CHECK(response_count == 1 && last_sequence == 61);
}

void SystemInit(void) {}

int main(void)
{
    uint8_t data[160] = {0};
    test_repeats(IR_PROTOCOL_NEC, 32, 1, 0);
    test_repeats(IR_PROTOCOL_NEC, 32, 3, 65535000U);
    test_repeats(IR_PROTOCOL_NEC, 32, 255, 0);
    test_repeats(IR_PROTOCOL_AEHA, 48, 3, 0);
    test_repeats(IR_PROTOCOL_AEHA, 1280, 255, 0);
    test_repeats(IR_PROTOCOL_SONY, 12, 3, 65535000U);
    test_repeats(IR_PROTOCOL_SONY, 15, 3, 0);
    test_repeats(IR_PROTOCOL_SONY, 20, 255, 0);
    test_long_aeha();
    test_requests();
    test_timeout();
    reset_test(0);
    IR_SendData(IR_PROTOCOL_NEC, data, 32, 0);
    CHECK(!IR_IsSending());
    IR_SendData(IR_PROTOCOL_SONY, data, 13, 1);
    CHECK(!IR_IsSending());
    IR_SendData(IR_PROTOCOL_AEHA, NULL, 48, 1);
    CHECK(!IR_IsSending());
    IR_SendNecRepeat();
    finish_task();
    CHECK(IR_TxSucceeded() && frame_count == 1 && repeats[0] && !marks[0]);
    printf("IR REPEAT TESTS PASSED (%u checks)\n", checks);
    return 0;
}
