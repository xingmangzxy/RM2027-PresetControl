#include "diagnostic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void packet(uint8_t p[25],const uint16_t ch[16],uint8_t flags,uint8_t footer) {
    memset(p,0,25);p[0]=15;p[23]=flags;p[24]=footer;
    for(unsigned c=0;c<16;++c)for(unsigned b=0;b<11;++b)
        if(ch[c]&(1u<<b))p[1+(c*11+b)/8]|=(uint8_t)(1u<<((c*11+b)%8));
}
static void feed(Diagnostic *d,uint32_t now,const uint8_t p[25]) {
    for(unsigned i=0;i<25;++i)diag_byte(d,now,p[i]);
    diag_tick(d,now);
}
int main(void) {
    Diagnostic d;uint16_t ch[16];uint8_t p[25];
    for(unsigned i=0;i<16;++i)ch[i]=(uint16_t)(i*123);
    diag_init(&d);packet(p,ch,0,0);
    for(unsigned n=0;n<3;++n)feed(&d,n*14,p);
    CHECK(d.data[D_RC_ONLINE]);CHECK(d.data[D_RC_FRAMES]==3);
    for(unsigned i=0;i<16;++i)CHECK(d.data[D_CHANNEL+i]==ch[i]);
    CHECK((int32_t)d.data[D_RIGHT_ROL]==-1000);
    CHECK((int32_t)d.data[D_LEFT_COL]==-1000);
    CHECK((int32_t)d.data[D_RIGHT_COL]==-1000);
    CHECK((int32_t)d.data[D_LEFT_ROL]==-976);
    diag_tick(&d,129);CHECK(!d.data[D_RC_ONLINE]);
    for(unsigned f=1;f<=3;++f) {
        packet(p,ch,(uint8_t)(f<<2),0);feed(&d,150+f,p);
        CHECK(!d.data[D_RC_ONLINE]);CHECK(d.data[D_RC_FLAGS]==f);
    }
    ch[0]=1695;ch[1]=353;ch[2]=1024;ch[3]=1695;ch[4]=353;ch[5]=1024;
    packet(p,ch,0,0);feed(&d,200,p);feed(&d,214,p);feed(&d,228,p);
    CHECK(d.data[D_RC_ONLINE]);CHECK(d.data[D_SB]==1);CHECK(d.data[D_SC]==2);
    CHECK(d.data[D_RIGHT_ROL]==1000);CHECK(d.data[D_RIGHT_COL]==0);
    CHECK((int32_t)d.data[D_LEFT_COL]==-1000);CHECK(d.data[D_LEFT_ROL]==1000);
    ch[4]=1695;ch[5]=0;packet(p,ch,0,0);feed(&d,242,p);
    CHECK(d.data[D_SB]==3);CHECK(d.data[D_SC]==0);
    diag_uart_error(&d);CHECK(!d.data[D_RC_ONLINE]);CHECK(d.data[D_UART_ERRORS]==1);
    for(unsigned footer=4;footer<=0x34;footer+=0x10) {
        packet(p,ch,0,(uint8_t)footer);feed(&d,300+footer,p);
        CHECK(d.data[D_RC_LAST]==300+footer);
    }
    packet(p,ch,0,0);p[24]=0x44;uint32_t frames=d.data[D_RC_FRAMES];feed(&d,400,p);
    CHECK(d.data[D_RC_FRAMES]==frames);CHECK(d.data[D_RC_BAD]>0);
    feed(&d,414,(packet(p,ch,0,0),p));CHECK(d.data[D_RC_FRAMES]==frames+1);
    /* A truncated frame followed after a gap resynchronizes. */
    for(unsigned i=0;i<12;++i)diag_byte(&d,450,p[i]);
    feed(&d,464,p);CHECK(d.data[D_RC_LAST]==464);
    /* Every channel/boundary exercises generic 11-bit packing. */
    for(unsigned c=0;c<16;++c)for(unsigned value=0;value<2048;++value) {
        ch[c]=(uint16_t)value;packet(p,ch,0,0);feed(&d,500,p);
        CHECK(d.data[D_CHANNEL+c]==value);
    }
    /* Noise then intact frames, streaming without inter-frame gap. */
    uint32_t rng=12345;
    for(unsigned n=0;n<200000;++n){rng=rng*1664525u+1013904223u;diag_byte(&d,510,(uint8_t)(rng>>24));CHECK(d.used<25);}
    for(unsigned n=0;n<5;++n)feed(&d,530+n*14,p);
    CHECK(d.data[D_RC_ONLINE]);
    /* Dual buses, IDs 1..8, signed RPM/current, timeout and tick rollover. */
    diag_init(&d);uint8_t motor[8]={0x1f,0xff,0xff,0x9c,0xff,0x38,42,0};
    for(unsigned b=0;b<2;++b)for(unsigned id=1;id<=8;++id) {
        diag_can(&d,0xfffffff0u,b,0x200+id,0,0,8,motor);
        unsigned m=D_MOTOR+(b*8+id-1)*8;
        CHECK(d.data[m]==1);CHECK(d.data[m+2]==8191);
        CHECK((int32_t)d.data[m+3]==-100);CHECK((int32_t)d.data[m+4]==-200);
        CHECK(d.data[m+5]==42);
    }
    diag_tick(&d,20);CHECK(d.data[D_BUS+6]==255);CHECK(d.data[D_BUS+22]==255);
    diag_tick(&d,400);CHECK(d.data[D_BUS+6]==0);CHECK(d.data[D_BUS+5]==255);
    for(unsigned b=0;b<2;++b) {
        diag_can(&d,500,b,0x200,0,0,8,motor);diag_can(&d,500,b,0x209,0,0,8,motor);
        diag_can(&d,500,b,0x201,1,0,8,motor);diag_can(&d,500,b,0x201,0,1,8,motor);
        diag_can(&d,500,b,0x201,0,0,7,motor);
        motor[0]=0x20;diag_can(&d,500,b,0x201,0,0,8,motor);motor[0]=0x1f;
        CHECK(d.data[D_BUS+b*16+9]==4);CHECK(d.data[D_BUS+b*16+2]==2);
        CHECK(d.data[D_MOTOR+b*64]==1);
    }
    diag_can(&d,500,2,0x201,0,0,8,motor);CHECK(d.data[D_MAGIC]==DIAG_MAGIC);
    diag_init(&d);packet(p,ch,0,0);
    feed(&d,0xffffffe0u,p);feed(&d,0xfffffff0u,p);feed(&d,0,p);CHECK(d.data[D_RC_ONLINE]);
    diag_tick(&d,101);CHECK(!d.data[D_RC_ONLINE]);
    printf("PASS: %u checks; channel unpacking, framing, flags, timeout, dual CAN IDs, signed feedback, wraparound.\n",checks);
    return 0;
}
