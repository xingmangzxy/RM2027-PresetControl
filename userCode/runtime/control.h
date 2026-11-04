#ifndef RC_CONTROL_H
#define RC_CONTROL_H
#include <stdint.h>
#include "config.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { RC_STOP, RC_ARMED_BENCH, RC_ARMED_DRIVE, RC_FAULT };
enum { F_NONE, F_CAN=1, F_TEMPERATURE, F_OVERSPEED, F_CURRENT,
       F_PROTOCOL, F_STALL, F_DIRECTION, F_LOOP, F_CAN_CONFLICT };
enum { WHY_BOOT, WHY_RADIO, WHY_FLAGS, WHY_ENABLE, WHY_MODE,
       WHY_NEUTRAL, WHY_FEEDBACK, WHY_LIMIT, WHY_KEY, WHY_GEOMETRY, WHY_FAULT,
       WHY_SC_STOP, WHY_SETTLE, WHY_STATIONARY, WHY_SB_STOP, WHY_KEY_HELD,
       WHY_BOOT_WAIT, WHY_DISABLED, WHY_SWITCH, WHY_ACTIVE, WHY_CAN_RECOVERY };
enum { CAN_ISSUE_GENERAL=1, CAN_ISSUE_FIFO, CAN_ISSUE_PASSIVE,
       CAN_ISSUE_BUSOFF, CAN_ISSUE_TX, CAN_ISSUE_RX, CAN_ISSUE_RESTART };
typedef struct {
    uint16_t channel[16];
    uint8_t frame[25], fill, healthy, flags;
    uint32_t last_byte, last_good, good_frames, bad_frames, streak;
} Radio;
typedef struct {
    int16_t rpm, current;
    uint8_t temperature;
    uint32_t stamp, count;
} Feedback;
typedef struct {
    Radio radio;
    Feedback motor[4];
    uint32_t state, fault, reason, bench_wheel, bench_mask, geometry_confirmed;
    uint32_t boot, last_step, armed_since, safe_since, key_since;
    uint32_t stall_since[4], direction_since[4];
    uint8_t safe_timing, ready, key_timing, key_previous, drive_enabled;
    uint8_t motion_started, prepare_mode, key_confirm_available;
    uint32_t arm_wait_ms, test_remaining_ms;
    int16_t output[4];
    float target[4], integral[4], x, y, w;
    uint32_t motion_qualify_ms;
    uint32_t confirm_wait_ms, fault_motor_id;
    int16_t fault_rpm, fault_current;
    uint8_t fault_temperature;
    uint32_t motion_seen, cycles;
    uint32_t can_recovering,can_hw_ok,can_good_since,can_good_timing;
    uint32_t can_incidents,can_last_esr,can_last_issue,can_wait_ms;
} Controller;
void rc_can_error(Controller *c,uint32_t now,unsigned issue,uint32_t esr);
void rc_can_health(Controller *c,uint32_t now);
void FrameInit(Controller *c);
void rc_init(Controller *c, uint32_t now, int drive_enabled);
void rc_uart_error(Controller *c);
int rc_byte(Controller *c, uint32_t now, uint8_t byte);
int rc_motor(Controller *c, uint32_t now, uint32_t id, int extended,
             int remote, unsigned length, const uint8_t *data);
void rc_fault(Controller *c, unsigned code);
void rc_step(Controller *c, uint32_t now, int key_pressed);
void rc_pack(const Controller *c, uint8_t data[8]);
void rc_mix(float x, float y, float w, float result[4]);
#ifdef __cplusplus
}
#endif
#endif
