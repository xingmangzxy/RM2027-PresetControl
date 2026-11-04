/* Diagnostic adaptation of devices/Src/RemoteControl.cpp.
 * Same CH1/2/3/4 -> right_rol/left_col/right_col/left_rol mapping.
 * Frame synchronization and raw 11-bit switch values replace the original DMA
 * buffer toggle and enum cast. This code only observes; it never controls motors. */
#include "diagnostic.h"
#include <string.h>

void diag_init(Diagnostic *d) {
    memset(d,0,sizeof(*d));
    d->data[D_MAGIC]=DIAG_MAGIC; d->data[D_VERSION]=DIAG_VERSION;
}
void diag_uart_error(Diagnostic *d) {
    ++d->data[D_UART_ERRORS]; d->used=0;
    d->data[D_RC_STREAK]=0; d->data[D_RC_ONLINE]=0;
}
static uint32_t switch_level(uint32_t ch) {
    /* Electrical levels, not physical UP/DOWN. Verify all three positions. */
    if(ch<100 || ch>1947) return 0;
    if(ch<700) return 1;
    if(ch>1350) return 3;
    return 2;
}
static uint32_t normalized(uint32_t ch) {
    int32_t n=((int32_t)ch-1024)*1000/671;
    if(n>1000)n=1000;
    if(n< -1000)n= -1000;
    return (uint32_t)n;
}
static int decode(Diagnostic *d,uint32_t now) {
    const uint8_t *p=d->frame;
    if(p[0]!=0x0f || !((p[24]==0) || ((p[24]&0xcf)==0x04)) || (p[23]&0xf0)) return 0;
    for(unsigned ch=0;ch<16;++ch) {
        unsigned bit=ch*11,at=1+bit/8,shift=bit%8;
        uint32_t v=p[at] | ((uint32_t)p[at+1]<<8);
        if(at+2<=22)v|=(uint32_t)p[at+2]<<16;
        d->data[D_CHANNEL+ch]=(v>>shift)&0x7ff;
    }
    ++d->data[D_RC_FRAMES]; d->data[D_RC_SEEN]=1;
    d->data[D_RC_LAST]=now; d->data[D_RC_FLAGS_RAW]=p[23];
    d->data[D_RC_FLAGS]=(p[23]>>2)&3;
    if(d->data[D_RC_FLAGS]) { d->data[D_RC_STREAK]=0; d->data[D_RC_ONLINE]=0; }
    else {
        d->data[D_RC_GOOD]=now;
        if(d->data[D_RC_STREAK]<1000)++d->data[D_RC_STREAK];
    }
    d->data[D_RIGHT_ROL]=normalized(d->data[D_CHANNEL]);
    d->data[D_LEFT_COL]=normalized(d->data[D_CHANNEL+1]);
    d->data[D_RIGHT_COL]=normalized(d->data[D_CHANNEL+2]);
    d->data[D_LEFT_ROL]=normalized(d->data[D_CHANNEL+3]);
    d->data[D_SB]=switch_level(d->data[D_CHANNEL+4]);
    d->data[D_SC]=switch_level(d->data[D_CHANNEL+5]);
    return 1;
}
void diag_byte(Diagnostic *d,uint32_t now,uint8_t value) {
    ++d->data[D_UART_BYTES];
    if(d->byte_seen && now-d->last_byte>4) {
        if(d->used){++d->data[D_RC_BAD];d->used=0;d->data[D_RC_STREAK]=0;}
        if(now-d->last_byte>100)d->data[D_RC_STREAK]=0;
        ++d->data[D_GAPS];
    }
    d->last_byte=now; d->byte_seen=1;
    if(!d->used && value!=0x0f) return;
    d->frame[d->used++]=value;
    if(d->used<25)return;
    if(decode(d,now)){d->used=0;return;}
    ++d->data[D_RC_BAD]; d->data[D_RC_STREAK]=0; d->data[D_RC_ONLINE]=0;
    unsigned start=1;
    while(start<25 && d->frame[start]!=0x0f)++start;
    d->used=25-start;
    memmove(d->frame,d->frame+start,d->used);
}
static int32_t signed16(const uint8_t *p) {
    uint32_t v=((uint32_t)p[0]<<8)|p[1];
    return v>=32768 ? (int32_t)v-65536 : (int32_t)v;
}
void diag_can(Diagnostic *d,uint32_t now,unsigned bus,uint32_t id,
              unsigned extended,unsigned remote,unsigned length,const uint8_t p[8]) {
    if(bus>=2)return;
    unsigned b=D_BUS+16*bus;
    ++d->data[b]; d->data[b+3]=now; d->data[b+4]=id;
    if(extended || remote || id<0x201 || id>0x208) {++d->data[b+9];return;}
    if(length!=8 || (((uint32_t)p[0]<<8)|p[1])>8191) {++d->data[b+2];return;}
    unsigned id0=id-0x201,m=D_MOTOR+(bus*8+id0)*8;
    ++d->data[b+1]; d->data[b+5]|=1u<<id0;
    ++d->data[m]; d->data[m+1]=now;
    d->data[m+2]=((uint32_t)p[0]<<8)|p[1];
    d->data[m+3]=(uint32_t)signed16(p+2); d->data[m+4]=(uint32_t)signed16(p+4);
    d->data[m+5]=p[6];
}
void diag_tick(Diagnostic *d,uint32_t now) {
    d->data[D_TIME]=now;
    /* Frame spacing may exceed four milliseconds; do not reset a healthy streak
     * just for ordinary SBUS inter-frame gaps. Long silence is an offline event. */
    if(d->data[D_RC_SEEN] && now-d->data[D_RC_LAST]>100) d->data[D_RC_STREAK]=0;
    d->data[D_RC_ONLINE]=d->data[D_RC_SEEN] && d->data[D_RC_STREAK]>=3 &&
        !d->data[D_RC_FLAGS] && now-d->data[D_RC_GOOD]<=100;
    for(unsigned bus=0;bus<2;++bus) {
        uint32_t mask=0;
        for(unsigned i=0;i<8;++i) {
            unsigned m=D_MOTOR+(bus*8+i)*8;
            d->data[m+6]=d->data[m] && now-d->data[m+1]<=300;
            if(d->data[m+6])mask|=1u<<i;
        }
        d->data[D_BUS+bus*16+6]=mask;
    }
}
