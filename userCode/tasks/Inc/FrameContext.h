#ifndef RM_FRAME_CONTEXT_H
#define RM_FRAME_CONTEXT_H
#include "control.h"
extern Controller *frameControl;
extern uint32_t frameNow;
extern int frameKey;
extern float frameWheelLinearLimit;
extern "C" void BoardTransmit(uint32_t now);
#endif
