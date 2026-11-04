// Adapted from the supplied RM2027 Device base class.
#ifndef RM_FRAME_C_DEVICE_H
#define RM_FRAME_C_DEVICE_H
#include <stdint.h>
enum DEVICE_TYPE_E { MOTOR, SERVO };
class Device {
protected:
    DEVICE_TYPE_E deviceType{};
    uint32_t deviceID{};
    virtual void Handle()=0;
    virtual void ErrorHandle()=0;
};
#endif
