#include "control.h"
#include "ChassisTask.h"
#include "RemoteControl.h"
#include "FrameContext.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <initializer_list>
static Controller c;
static uint32_t now;
static unsigned checks,scenarios,sends;
static int sa_value=353;
static float left_vertical=0;
extern "C" volatile unsigned servo_pulse_us, servo2_pulse_us, servo3_pulse_us, servo4_pulse_us;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
extern "C" void BoardTransmit(uint32_t){++sends;}
static void pass(const char *s){++scenarios;std::printf("PASS %s\n",s);}
static const unsigned ids[]={MOTOR_ID_FL,MOTOR_ID_FR,MOTOR_ID_BL,MOTOR_ID_BR};
static void radio(int sb,int sc,float x,float y,float w,int flags=0){
    uint16_t ch[16];for(auto &v:ch)v=1024;
    ch[6]=static_cast<uint16_t>(sa_value);ch[2]=static_cast<uint16_t>(1024+left_vertical*660);
    ch[1]=static_cast<uint16_t>(1024-y*660);ch[3]=static_cast<uint16_t>(1024+x*660);
    ch[0]=static_cast<uint16_t>(1024-w*660);
    ch[4]=sb==0 ? 353 : sb==1 ? 1024 : 1695;
    ch[5]=sc==0 ? 353 : sc==1 ? 1024 : 1695;
    uint8_t p[25]={};p[0]=0x0f;p[23]=static_cast<uint8_t>(flags);
    for(unsigned i=0;i<16;++i)for(unsigned b=0;b<11;++b)
        if(ch[i]&(1u<<b))p[1+(i*11+b)/8]|=1u<<((i*11+b)%8);
    for(auto v:p)rc_byte(&c,now,v);
}
static void feedback(unsigned wheel,int rpm,int temperature=25){
    uint8_t d[8]={};d[2]=static_cast<uint16_t>(rpm)>>8;d[3]=rpm;d[6]=temperature;
    CHECK(rc_motor(&c,now,0x200+ids[wheel],0,0,8,d));
}
static void tick(int sb,int sc,float x=0,float y=0,float w=0,bool plant=true,int key=0){
    now+=2;if(now%10==0)radio(sb,sc,x,y,w);
    for(unsigned i=0;i<4;++i)feedback(i,plant ? static_cast<int>(c.target[i]*0.85f) : 0);
    rc_step(&c,now,key);
    for(auto u:c.output)CHECK(std::abs(static_cast<int>(u))<=CURRENT_LIMIT);
}
static void init(){left_vertical=0;sa_value=353;now=0;rc_init(&c,now,1);FrameInit(&c);}
static void zero(){uint8_t b[8];rc_pack(&c,b);for(auto u:b)CHECK(!u);for(auto u:c.output)CHECK(!u);}
static void arm(int mode=2){
    init();for(unsigned n=0;n<300;++n)tick(mode,1);
    CHECK(c.state==RC_ARMED_DRIVE && !c.fault);zero();
}
int main(){
    CHECK(CMFL.Slot()==1);arm();CHECK(c.state==RC_ARMED_DRIVE);zero();
    for(unsigned n=0;n<10;++n){tick(2,0);}CHECK(c.state==RC_STOP);zero();
    pass("SC low stops chassis");
    arm();for(unsigned n=0;n<10;++n)tick(2,2);CHECK(servo_pulse_us==1235);zero();
    for(unsigned n=0;n<100;++n){tick(2,2,0,0,-0.8f);}unsigned fast=servo_pulse_us;CHECK(fast>1235);zero();
    sa_value=1695;for(unsigned n=0;n<100;++n){tick(2,2,0,0,-0.8f);}unsigned slow=servo_pulse_us-fast;CHECK(slow>0 && slow<fast-1235);
    for(unsigned n=0;n<10;++n){tick(2,2);}
    unsigned prior=servo_pulse_us;for(unsigned n=0;n<10;++n){tick(1,2);}CHECK(servo_pulse_us==prior);
    for(unsigned n=0;n<10;++n){tick(1,0);}CHECK(servo_pulse_us==0 && servo2_pulse_us==0 && servo3_pulse_us==0 && servo4_pulse_us==0);zero();
    pass("servo modes adjust at different rates and uncalibrated presets do not move");
    arm();for(unsigned n=0;n<30;++n)tick(2,1,0.7f);CHECK(c.output[0]>0);sa_value=1024;for(unsigned n=0;n<10;++n)tick(2,1);zero();
    pass("invalid SD disables outputs");
    std::printf("RESULT: %u scenarios, %u checks passed.\n",scenarios,checks);
}
