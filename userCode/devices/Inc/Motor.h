// Official Motor / PID interface, adapted for the four M3508 chassis motors.
#ifndef RM_FRAME_C_MOTOR_H
#define RM_FRAME_C_MOTOR_H
#include "Device.h"
#include "control.h"
#define MOTOR_ID_1 0x01u
#define MOTOR_ID_2 0x02u
#define MOTOR_ID_3 0x04u
#define MOTOR_ID_4 0x08u
enum MOTOR_CTRL_TYPE_e { SPEED_Single,POSITION_Double };
struct PID_Regulator_t {
    float kp{},ki{},kd{},componentKpMax{},componentKiMax{},componentKdMax{},outputMax{};
    float ref{},fdb{},errSum{},output{};
};
struct MOTOR_INIT_t {
    PID_Regulator_t *speedPIDp{},*anglePIDp{};
    uint32_t _motorID{};
    float reductionRatio{MOTOR_REDUCTION};
    MOTOR_CTRL_TYPE_e ctrlType{SPEED_Single};
};
struct MOTOR_STATE_t { float speed{},angle{},moment{},temperature{}; };
class PID {
public:
    PID_Regulator_t PIDInfo;
    float PIDCalc(float target,float feedback);
    void Clear();
};
class Motor : private Device {
public:
    static Motor *motorPtrs[2][8];
    static int16_t motor_intensity[16];
    static uint32_t motor_IDs;
    static void Init(Controller *context);
    static void CANPackageSend();
    uint8_t stopFlag{1};
    PID speedPID;
    MOTOR_STATE_t state;
    float targetSpeed{},reductionRatio{MOTOR_REDUCTION};
    Motor(uint32_t id,MOTOR_INIT_t *init);
    void Handle() override;
    void ErrorHandle() override {}
    void SetTargetSpeed(float rpm);
    void Stop();
    void Brake();
    unsigned Slot()const{return slot;}
private:
    unsigned slot{},wheel{};
    float ramped{};
};
#endif
