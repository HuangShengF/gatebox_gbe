#include "gbe_protocol.h"
#include "gb_protocol.h"
static void gbe_protocol_pc_request_motion(const Frame_t *frame)
{

	
}
static void gbe_protocol_pc_request_ir_tansimit(const Frame_t *frame)
{

	
}
gb_request_callback_t gb_callback[2] = {NULL};
void gbe_protocol_init(void)
{
    // 注册回调给下层gb.c使用,因为下层不能直接调用上层函数，防止重复依赖
    gb_callback[0] = gbe_protocol_pc_request_motion;
    gb_callback[1] = gbe_protocol_pc_request_ir_tansimit;
    gb_protocol_register_callback(gb_callback, 2);
    gb_protocol_init();
}
