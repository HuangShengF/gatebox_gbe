#ifndef __GBE_PROTOCOL_H
#define __GBE_PROTOCOL_H
#include "n32l40x.h"
#include "log.h"

#define GBE_ALS_UPLOAD_INTERVAL_MS    500U
#define GBE_ALS_MAX_LUX               64000.0f
#define GBE_ALS_P_FACTOR              1.0f

void gbe_protocol_init(void);
void gbe_protocol_poll(void);
#endif
