#include "control.h"
#include <string.h>
static const unsigned ids[4]={MOTOR_ID_FL,MOTOR_ID_FR,MOTOR_ID_BL,MOTOR_ID_BR};
static int16_t word(uint8_t a,uint8_t b) {
    int n=a*256+b; return (int16_t)(n>=32768 ? n-65536 : n);
}
static void stop(Controller *c,unsigned why,int unused) {
    (void)unused;c->state=c->fault ? RC_FAULT : RC_STOP;
    c->reason=why;c->ready=0;c->safe_timing=0;
    memset(c->output,0,sizeof(c->output));memset(c->target,0,sizeof(c->target));
}
void rc_init(Controller *c,uint32_t now,int enabled) {
    memset(c,0,sizeof(*c));c->boot=c->last_step=now;c->drive_enabled=enabled==1;
    c->can_hw_ok=1;
}
void rc_fault(Controller *c,unsigned code) {
    if(code==F_CAN){rc_can_error(c,c->last_step,CAN_ISSUE_GENERAL,0);return;}
    if(!c->fault)c->fault=code;
    stop(c,WHY_FAULT,0);
}
void rc_can_error(Controller *c,uint32_t now,unsigned issue,uint32_t esr) {
    if(!c->can_recovering)++c->can_incidents;
    c->can_recovering=1;c->can_hw_ok=0;c->can_good_timing=0;
    c->can_good_since=now;c->can_wait_ms=CAN_STABLE_MS;
    c->can_last_issue=issue;c->can_last_esr=esr;
    for(unsigned i=0;i<4;++i)c->motor[i].count=0;
    stop(c,WHY_CAN_RECOVERY,0);
}
void rc_can_health(Controller *c,uint32_t now) {
    if(!c->can_recovering)return;
    int good=c->can_hw_ok!=0;
    for(unsigned i=0;i<4;++i)
        if(c->motor[i].count<10 || now-c->motor[i].stamp>MOTOR_TIMEOUT_MS)good=0;
    if(!good){c->can_good_timing=0;c->can_wait_ms=CAN_STABLE_MS;return;}
    if(!c->can_good_timing){c->can_good_timing=1;c->can_good_since=now;}
    uint32_t elapsed=now-c->can_good_since;
    c->can_wait_ms=elapsed>=CAN_STABLE_MS ? 0 : CAN_STABLE_MS-elapsed;
    if(!c->can_wait_ms){c->can_recovering=0;c->can_good_timing=0;}
}
void rc_uart_error(Controller *c) {
    c->radio.healthy=0; c->radio.streak=0; c->radio.fill=0;
    ++c->radio.bad_frames;
    stop(c,WHY_RADIO,0);
}
int rc_byte(Controller *c,uint32_t now,uint8_t b) {
    Radio *r=&c->radio;
    if(now-r->last_byte>RC_TIMEOUT_MS) {r->healthy=0;r->streak=0;}
    if(r->fill && now-r->last_byte>4) {
        r->fill=0;r->healthy=0;r->streak=0;
        stop(c,WHY_RADIO,0);
    }
    r->last_byte=now;
    if(!r->fill && b!=0x0f) return 0;
    r->frame[r->fill++]=b;
    if(r->fill<25) return 0;
    r->fill=0;
    uint8_t *f=r->frame;
    /* Standard SBUS footer or the four SBUS2 slot footers, no fast SBUS. */
    int footer=f[24]==0 || (f[24]&0xcf)==0x04;
    uint16_t channels[16];
    for(unsigned ch=0;ch<16;++ch) {
        unsigned bit=ch*11, pos=1+bit/8, shift=bit%8;
        uint32_t bits=f[pos]|(uint32_t)f[pos+1]<<8;
        if(pos+2<=22) bits|=(uint32_t)f[pos+2]<<16;
        channels[ch]=(uint16_t)((bits>>shift)&0x7ff);
    }
    int range=1;
    const unsigned used[]={0,1,2,3,4,5};
    for(unsigned n=0;n<6;++n) if(channels[used[n]]<160 || channels[used[n]]>1888) range=0;
    if(!footer || (f[23]&0xf0) || !range) {
        ++r->bad_frames; r->healthy=0; r->streak=0;
        stop(c,WHY_RADIO,0);
        /* Find a possible next header within the rejected packet. */
        for(unsigned i=1;i<25;++i) if(f[i]==0x0f) {
            r->fill=(uint8_t)(25-i); memmove(f,f+i,r->fill); break;
        }
        return -1;
    }
    memcpy(r->channel,channels,sizeof(channels)); r->flags=f[23];
    if(f[23]&0x0c) {
        r->healthy=0; r->streak=0;
        stop(c,WHY_FLAGS,0); return -1;
    }
    r->last_good=now; r->healthy=1;
    if(r->streak<1000000) ++r->streak;
    ++r->good_frames;
    return 1;
}
int rc_motor(Controller *c,uint32_t now,uint32_t id,int extended,int remote,
             unsigned length,const uint8_t *d) {
    if(extended || remote) return 0;
    if(id>=0x205 && id<=0x208) return 0;
    if(id<0x201 || id>0x204) return 0;
    if(length!=8 || !d || ((unsigned)d[0]*256+d[1])>8191) {
        rc_fault(c,F_PROTOCOL); return 0;
    }
    unsigned wheel=0;
    while(wheel<4 && ids[wheel]!=id-0x200) ++wheel;
    if(wheel==4) { rc_fault(c,F_PROTOCOL); return 0; }
    Feedback *m=&c->motor[wheel];
    m->rpm=word(d[2],d[3]); m->current=word(d[4],d[5]);
    m->temperature=d[6]; m->stamp=now;
    if(m->count<0xffffffffu) ++m->count;
    return 1;
}
void rc_pack(const Controller *c,uint8_t d[8]) {
    for(unsigned i=0;i<4;++i) {
        int16_t v=(c->drive_enabled && !c->fault &&
                   (c->state==RC_ARMED_DRIVE || (c->state==RC_ARMED_BENCH && i==c->bench_wheel))) ? c->output[i] : 0;
        if(v>CURRENT_LIMIT) v=CURRENT_LIMIT;
        if(v<-CURRENT_LIMIT) v=-CURRENT_LIMIT;
        unsigned slot=ids[i]-1;
        d[slot*2]=(uint8_t)((uint16_t)v>>8); d[slot*2+1]=(uint8_t)v;
    }
}
