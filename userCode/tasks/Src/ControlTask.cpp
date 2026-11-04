// Normal remote mode in the supplied framework's task sequence.
#include "ControlTask.h"
#include "ChassisTask.h"
#include "RemoteControl.h"
#include "UserTask.h"
#include "FrameContext.h"
#include <cstdlib>
Controller *frameControl=nullptr;
uint32_t frameNow=0;
int frameKey=0;
float frameWheelLinearLimit=NORMAL_LINEAR_MPS;
extern "C" { volatile unsigned servo_pulse_us=0, servo2_pulse_us=0, servo3_pulse_us=0, servo4_pulse_us=0; }
static float servo_target=1235.0f, servo2_target=1500.0f, servo3_target=1500.0f, servo4_target=1500.0f;
static int last_mode=-1;
static bool mode_ready=false;
static bool moving_command=false, braking=false;
static uint32_t brake_start=0;
/* Uncalibrated presets are disabled until captured and deliberately filled. */
static const unsigned presets[3][4]={{0,0,0,0},{0,0,0,0},{0,0,0,0}};
static int last_sb=-1;
static float approach(float current,float target,float step) {
    if(target>current+step)return current+step;
    if(target<current-step)return current-step;
    return target;
}
static float goal[4]={1235,1500,1500,1500};
static void stopped(unsigned reason,bool forgetStop) {
    moving_command=false;braking=false;
    auto &c=*frameControl;c.state=c.fault ? RC_FAULT : RC_STOP;c.reason=reason;
    if(forgetStop)c.ready=0;
    ChassisStop();UserStop();
}
void CtrlHandle(){
    auto &c=*frameControl;auto &r=RemoteControl::rcInfo;
    unsigned sd=c.radio.channel[6];
    int speed=sd>=160 && sd<600 ? 0 : sd>1400 && sd<=1888 ? 1 : -1;
    int mode=r.sRight==MID_POS ? 0 : r.sRight==DOWN_POS ? 1 : -1;
    int sb=r.sLeft==UP_POS ? 0 : r.sLeft==MID_POS ? 1 : r.sLeft==DOWN_POS ? 2 : -1;
    if(mode!=last_mode){last_mode=mode;mode_ready=false;last_sb=-1;servo_pulse_us=servo2_pulse_us=servo3_pulse_us=servo4_pulse_us=0;stopped(WHY_NEUTRAL,true);}
    bool all_neutral=RemoteControl::Neutral() && r.left_col==0;
    if(mode<0 || speed<0 || sb<0 || !RemoteControl::Healthy(c.radio,frameNow) || frameKey || c.fault){
        servo_pulse_us=servo2_pulse_us=servo3_pulse_us=servo4_pulse_us=0;mode_ready=false;last_sb=-1;
        stopped(!RemoteControl::Healthy(c.radio,frameNow) ? WHY_RADIO : WHY_SWITCH,true);return;
    }
    if(!mode_ready){if(!all_neutral){stopped(WHY_NEUTRAL,true);return;}mode_ready=true;}
    if(mode==1){
        stopped(WHY_DISABLED,true);
        if(last_sb<0)last_sb=sb;
        else if(sb!=last_sb){
            last_sb=sb;
            if(speed==0 && presets[sb][0] && presets[sb][1] && presets[sb][2] && presets[sb][3])
                for(unsigned i=0;i<4;++i)goal[i]=(float)presets[sb][i];
        }
        const float rate=speed==0 ? 400.0f : 100.0f;
        float axes[]={r.left_rol,r.left_col,r.right_rol,r.right_col};
        for(unsigned i=0;i<4;++i)goal[i]+=axes[i]*rate*LOOP_SECONDS;
        const float lo[]={1049,1000,1000,1000};
        for(unsigned i=0;i<4;++i){if(goal[i]<lo[i])goal[i]=lo[i];if(goal[i]>2000)goal[i]=2000;}
        servo_target=approach(servo_target,goal[0],400.0f*LOOP_SECONDS);
        servo2_target=approach(servo2_target,goal[1],400.0f*LOOP_SECONDS);
        servo3_target=approach(servo3_target,goal[2],400.0f*LOOP_SECONDS);
        servo4_target=approach(servo4_target,goal[3],400.0f*LOOP_SECONDS);
        servo_pulse_us=(unsigned)servo_target;servo2_pulse_us=(unsigned)servo2_target;
        servo3_pulse_us=(unsigned)servo3_target;servo4_pulse_us=(unsigned)servo4_target;
        return;
    }
    servo_pulse_us=servo2_pulse_us=servo3_pulse_us=servo4_pulse_us=0;
    if(c.fault){stopped(WHY_FAULT,true);return;}
    if(c.can_recovering){stopped(WHY_CAN_RECOVERY,true);return;}
    if(!RemoteControl::Healthy(c.radio,frameNow)){stopped(c.radio.flags&0x0c ? WHY_FLAGS : WHY_RADIO,true);return;}
    for(unsigned i=0;i<4;++i)if(c.motor[i].count<10 || frameNow-c.motor[i].stamp>MOTOR_TIMEOUT_MS){stopped(WHY_FEEDBACK,true);return;}
    if(!c.drive_enabled){stopped(WHY_DISABLED,true);return;}
    bool neutral=RemoteControl::Neutral();
    bool stop=r.sRight==UP_POS;
    if(stop){stopped(WHY_ENABLE,true);return;}
    if(r.sRight!=MID_POS || r.sLeft==INVALID_POS){stopped(WHY_SWITCH,true);return;}

    if(frameKey){stopped(WHY_KEY,true);return;}
    if(c.state!=RC_ARMED_DRIVE){
        /* Direct start: no remembered SC-endpoint acknowledgement. Healthy
         * receiver/ESCs, SC center, a drive SB position and neutral sticks are
         * sufficient, including after link recovery. A live off-center stick
         * cannot restart a stopped vehicle until it passes neutral. */
        if(!neutral){stopped(WHY_NEUTRAL,false);return;}
        c.state=RC_ARMED_DRIVE;c.ready=0;
    }
    c.reason=WHY_ACTIVE;
    float linear=speed==0 ? SLOW_LINEAR_MPS : NORMAL_LINEAR_MPS;
    float yaw=speed==0 ? SLOW_YAW_RPM : NORMAL_YAW_RPM;
    frameWheelLinearLimit=linear;
    c.x=r.right_col;c.y=-r.right_rol;c.w=-r.left_rol;
    if(neutral){
        if(moving_command){moving_command=false;braking=true;brake_start=frameNow;}
        if(braking && frameNow-brake_start<BRAKE_DURATION_MS)ChassisBrake();
        else {braking=false;ChassisStop();}
        return;
    }
    braking=false;moving_command=true;
    ChassisSetVelocity(c.x*linear,c.y*linear,c.w*yaw);
}
extern "C" void rc_step(Controller *c,uint32_t now,int key){
    frameControl=c;frameNow=now;frameKey=key;
    uint32_t dt=now-c->last_step;c->last_step=now;++c->cycles;
    if(dt>10)rc_fault(c,F_LOOP);
    RemoteControl::Update(c->radio);
    rc_can_health(c,now);
    CtrlHandle();
    ChassisHandle();
    UserHandle();
    Motor::CANPackageSend();
}

extern "C" void FrameInit(Controller *c){last_mode=-1;last_sb=-1;mode_ready=false;goal[0]=1235;goal[1]=goal[2]=goal[3]=1500;servo_target=1235;servo2_target=1500;servo3_target=1500;servo4_target=1500;servo_pulse_us=0;servo2_pulse_us=0;servo3_pulse_us=0;servo4_pulse_us=0;moving_command=false;braking=false;Motor::Init(c);ChassisStart();UserInit();}
