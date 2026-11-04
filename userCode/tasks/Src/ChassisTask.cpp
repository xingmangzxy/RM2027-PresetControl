// Implements the missing four-wheel equations in the supplied ChassisTask.cpp.
#include "ChassisTask.h"
#include "FrameContext.h"
#include <cmath>
static PID_Regulator_t parameters() {
    PID_Regulator_t p{};p.kp=SPEED_KP;p.ki=SPEED_KI;
    p.componentKpMax=p.componentKiMax=p.outputMax=CURRENT_LIMIT;return p;
}
PID_Regulator_t pidRegulator=parameters();
static MOTOR_INIT_t motorInit(){MOTOR_INIT_t m{};m.speedPIDp=&pidRegulator;m.reductionRatio=MOTOR_REDUCTION;return m;}
MOTOR_INIT_t chassisMotorInit=motorInit();
Motor CMFL(1u<<(MOTOR_ID_FL-1),&chassisMotorInit);
Motor CMFR(1u<<(MOTOR_ID_FR-1),&chassisMotorInit);
Motor CMBL(1u<<(MOTOR_ID_BL-1),&chassisMotorInit);
Motor CMBR(1u<<(MOTOR_ID_BR-1),&chassisMotorInit);
uint8_t ChassisStopFlag=1;
float FBVelocity=0,LRVelocity=0,RTVelocity=0;
void ChassisStart(){ChassisStop();}
void ChassisSetVelocity(float fb,float lr,float rt){
    ChassisStopFlag=0;FBVelocity=fb;LRVelocity=lr;RTVelocity=rt;
}
void ChassisStop(){
    ChassisStopFlag=1;FBVelocity=LRVelocity=RTVelocity=0;
    CMFL.Stop();CMFR.Stop();CMBL.Stop();CMBR.Stop();
}
void ChassisBrake(){
    ChassisStopFlag=1;FBVelocity=LRVelocity=RTVelocity=0;
    CMFL.Brake();CMFR.Brake();CMBL.Brake();CMBR.Brake();
}
void WheelsSpeedCalc(float fb,float lr,float rt) {
    const float pi=3.14159265358979323846f;
    const float k=(TRACK_WIDTH_M+WHEEL_BASE_M)*0.5f;
    const float yaw=rt*pi/30.0f;
    float v[]={fb-lr-k*yaw,fb+lr+k*yaw,fb+lr-k*yaw,fb-lr+k*yaw};
    float peak=frameWheelLinearLimit;
    for(float x:v)if(std::fabs(x)>peak)peak=std::fabs(x);
    float scale=peak>0 ? frameWheelLinearLimit/peak : 0;
    const float factor=60.0f/(pi*WHEEL_DIAMETER_M);
    CMFL.SetTargetSpeed(v[0]*scale*factor*MOTOR_SIGN_FL);
    CMFR.SetTargetSpeed(v[1]*scale*factor*MOTOR_SIGN_FR);
    CMBL.SetTargetSpeed(v[2]*scale*factor*MOTOR_SIGN_BL);
    CMBR.SetTargetSpeed(v[3]*scale*factor*MOTOR_SIGN_BR);
}
void ChassisHandle(){
    if(!ChassisStopFlag)WheelsSpeedCalc(FBVelocity,LRVelocity,RTVelocity);
    CMFL.Handle();CMFR.Handle();CMBL.Handle();CMBR.Handle();
}
