// Supplied framework chassis task API.
#ifndef RM_FRAME_C_CHASSISTASK_H
#define RM_FRAME_C_CHASSISTASK_H
#include "Motor.h"
extern Motor CMFL,CMFR,CMBL,CMBR;
extern uint8_t ChassisStopFlag;
extern float FBVelocity,LRVelocity,RTVelocity;
void ChassisStart();
void ChassisHandle();
void ChassisSetVelocity(float fb,float lr,float rt);
void ChassisStop();
void ChassisBrake();
void WheelsSpeedCalc(float fb,float lr,float rt);
#endif
