// Adapted from the supplied Motor.cpp. CAN transport uses the validated board driver.
#include "Motor.h"
#include "FrameContext.h"
#include <cmath>
Motor *Motor::motorPtrs[2][8]={};
int16_t Motor::motor_intensity[16]={};
uint32_t Motor::motor_IDs=0;
static float clip(float v,float lo,float hi){return v<lo ? lo : v>hi ? hi : v;}
void PID::Clear(){PIDInfo.errSum=PIDInfo.output=0;}
float PID::PIDCalc(float target,float feedback) {
    auto &p=PIDInfo;p.ref=target;p.fdb=feedback;
    float err=target-feedback;
    float lo=target<0 ? -p.outputMax : -BRAKE_CURRENT_LIMIT;
    float hi=target>0 ? p.outputMax : BRAKE_CURRENT_LIMIT;
    float candidate=clip(p.errSum+p.ki*err*LOOP_SECONDS,-p.componentKiMax,p.componentKiMax);
    float requested=clip(p.kp*err,-p.componentKpMax,p.componentKpMax)+candidate;
    if((requested>=lo && requested<=hi)||(requested>hi && err<0)||(requested<lo && err>0))p.errSum=candidate;
    p.output=target==0 ? 0 : clip(clip(p.kp*err,-p.componentKpMax,p.componentKpMax)+p.errSum,lo,hi);
    if(target==0)Clear();
    return p.output;
}
Motor::Motor(uint32_t id,MOTOR_INIT_t *init) {
    deviceID=id;deviceType=MOTOR;
    while(slot<7 && !(id&(1u<<slot)))++slot;
    motorPtrs[0][slot]=this;motor_IDs|=id;
    if(init->speedPIDp)speedPID.PIDInfo=*init->speedPIDp;
    reductionRatio=init->reductionRatio;
    const unsigned ids[]={MOTOR_ID_FL,MOTOR_ID_FR,MOTOR_ID_BL,MOTOR_ID_BR};
    while(wheel<3 && ids[wheel]!=slot+1)++wheel;
}
void Motor::Init(Controller *context) {
    frameControl=context;
    for(unsigned i=0;i<4;++i)if(motorPtrs[0][i])motorPtrs[0][i]->Stop();
}
void Motor::SetTargetSpeed(float rpm) {
    if(rpm==0){Stop();return;}
    stopFlag=0;targetSpeed=rpm;
}
void Motor::Stop() {
    stopFlag=1;targetSpeed=ramped=0;speedPID.Clear();
    motor_intensity[slot]=0;
    if(frameControl){frameControl->output[wheel]=0;frameControl->target[wheel]=0;}
}
void Motor::Brake() {
    stopFlag=2;targetSpeed=ramped=0;speedPID.Clear();
}
void Motor::Handle() {
    if(!frameControl)return;
    auto &f=frameControl->motor[wheel];state.speed=f.rpm/reductionRatio;
    state.moment=f.current;state.temperature=f.temperature;
    if(stopFlag==2){
        float cmd=std::fabs(state.speed)<=BRAKE_DEADZONE_RPM ? 0.0f :
            clip(-BRAKE_GAIN*state.speed,-BRAKE_CURRENT_LIMIT,BRAKE_CURRENT_LIMIT);
        motor_intensity[slot]=static_cast<int16_t>(cmd);
        frameControl->output[wheel]=motor_intensity[slot];
        frameControl->target[wheel]=0;
        return;
    }
    if(stopFlag){Stop();return;}
    float previous=ramped;
    ramped+=clip(targetSpeed-ramped,-WHEEL_ACCEL_RPM_S*LOOP_SECONDS,WHEEL_ACCEL_RPM_S*LOOP_SECONDS);
    if(previous*ramped<=0)speedPID.Clear();
    int16_t output=static_cast<int16_t>(speedPID.PIDCalc(ramped,state.speed));
    motor_intensity[slot]=output;frameControl->output[wheel]=output;
    frameControl->target[wheel]=ramped*reductionRatio;
}
void Motor::CANPackageSend(){BoardTransmit(frameNow);}
