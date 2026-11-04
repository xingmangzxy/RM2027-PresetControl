// Official RemoteControl / rcInfo interface, with validated 11-bit switch decoding.
#ifndef RM_FRAME_C_REMOTECONTROL_H
#define RM_FRAME_C_REMOTECONTROL_H
#include "Device.h"
#include "control.h"
enum SWITCH_STATE_E { UP_POS=1, MID_POS=2, DOWN_POS=3, INVALID_POS=0 };
struct RC_Info_t {
    float left_col{},left_rol{},right_col{},right_rol{};
    SWITCH_STATE_E sLeft{INVALID_POS},sRight{INVALID_POS};
};
class RemoteControl {
public:
    static RC_Info_t rcInfo;
    static void Update(const Radio &radio);
    static bool Healthy(const Radio &radio,uint32_t now);
    static bool Neutral();
};
#endif
