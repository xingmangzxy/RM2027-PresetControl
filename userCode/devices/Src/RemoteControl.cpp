// Adapted from the supplied RemoteControl.cpp. UART/DMA is initialized by BoardMain.c.
#include "RemoteControl.h"
RC_Info_t RemoteControl::rcInfo;
static float axis(uint16_t raw) {
    float v=((int)raw-RC_CENTER)/RC_SPAN;
    if(v>1)v=1;
    if(v< -1)v=-1;
    float a=v<0 ? -v : v;
    return a<=RC_DEADZONE ? 0 : (v<0 ? -1.0f : 1.0f)*(a-RC_DEADZONE)/(1-RC_DEADZONE);
}
static SWITCH_STATE_E level(uint16_t raw) {
    if(raw<160 || raw>1888)return INVALID_POS;
    if(raw<600)return UP_POS;
    if(raw>1400)return DOWN_POS;
    if(raw>=800 && raw<=1248)return MID_POS;
    return INVALID_POS;
}
void RemoteControl::Update(const Radio &r) {
    rcInfo.right_rol=axis(r.channel[0])*AXIS_SIGN_Y;
    rcInfo.left_col=axis(r.channel[1]);
    rcInfo.right_col=axis(r.channel[2])*AXIS_SIGN_X;
    rcInfo.left_rol=axis(r.channel[3])*AXIS_SIGN_W;
    rcInfo.sLeft=level(r.channel[4]);rcInfo.sRight=level(r.channel[5]);
}
bool RemoteControl::Healthy(const Radio &r,uint32_t now) {
    return r.healthy && r.streak>=20 && !(r.flags&0x0c) && now-r.last_good<=RC_TIMEOUT_MS;
}
bool RemoteControl::Neutral() {
    return rcInfo.right_col==0 && rcInfo.right_rol==0 && rcInfo.left_rol==0;
}
