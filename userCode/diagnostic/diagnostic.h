#ifndef RM_DIAGNOSTIC_H
#define RM_DIAGNOSTIC_H
#include <stdint.h>
#define DIAG_WORDS 256u
#define DIAG_MAGIC 0x524d4631u
#define DIAG_VERSION 2u
/* Published uint32_t ABI: see layout.json. No drive command exists in this module. */
enum { D_MAGIC=0,D_VERSION,D_SEQUENCE,D_TIME,D_LOOPS,D_UART_BYTES,D_UART_ERRORS,
       D_RC_FRAMES,D_RC_BAD,D_RC_LAST,D_RC_GOOD,D_RC_FLAGS,D_RC_SEEN,D_RC_ONLINE,
       D_RC_STREAK,D_RC_FLAGS_RAW,D_CHANNEL=16,D_RIGHT_COL=32,D_RIGHT_ROL,
       D_LEFT_COL,D_LEFT_ROL,D_SB,D_SC,D_RESET,D_GAPS,D_BUS=48,D_MOTOR=80 };
typedef struct {
    uint32_t data[DIAG_WORDS];
    uint8_t frame[25];
    uint32_t used,last_byte,byte_seen;
} Diagnostic;
void diag_init(Diagnostic *d);
void diag_uart_error(Diagnostic *d);
void diag_byte(Diagnostic *d,uint32_t now,uint8_t value);
void diag_can(Diagnostic *d,uint32_t now,unsigned bus,uint32_t id,
              unsigned extended,unsigned remote,unsigned length,const uint8_t bytes[8]);
void diag_tick(Diagnostic *d,uint32_t now);
#endif
