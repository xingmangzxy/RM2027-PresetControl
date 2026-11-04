#ifndef RC_CONFIG_H
#define RC_CONFIG_H
/* CAN1 physical wheel IDs, as verified by the operator. */
#define MOTOR_ID_FL 2
#define MOTOR_ID_FR 1
#define MOTOR_ID_BL 3
#define MOTOR_ID_BR 4
#define MOTOR_SIGN_FL 1
#define MOTOR_SIGN_FR -1
#define MOTOR_SIGN_BL 1
#define MOTOR_SIGN_BR -1
#define AXIS_SIGN_X 1
#define AXIS_SIGN_Y 1
#define AXIS_SIGN_W 1
#define RC_CENTER 1024
#define RC_SPAN 660.0f
#define RC_DEADZONE 0.08f
#define RC_TIMEOUT_MS 100u
#define MOTOR_TIMEOUT_MS 50u
#define CONTROL_LOGIC_REVISION 7u
#define GROUND_TEST_PROFILE 4u
#define DIRECT_START 1u
#define CAN_STABLE_MS 300u
#define CAN_RETRY_MS 500u
#define CAN_STAGE_TIMEOUT_MS 50u
#define TEMPERATURE_STOP_ENABLED 0
/* Normal controller configuration, not bench-test interlocks. Geometry is
 * inherited from the supplied framework and must match the actual chassis. */
#define WHEEL_DIAMETER_M 0.1525f
#define TRACK_WIDTH_M 0.40f
#define WHEEL_BASE_M 0.38f
#define MOTOR_REDUCTION 19.2f
#define SLOW_LINEAR_MPS 0.30f
#define NORMAL_LINEAR_MPS 1.80f
#define SLOW_YAW_RPM 7.5f
#define NORMAL_YAW_RPM 27.0f
#define WHEEL_ACCEL_RPM_S 400.0f
#define BRAKE_DURATION_MS 500u
#define BRAKE_CURRENT_LIMIT 800.0f /* command units, bounded feedback braking */
#define BRAKE_GAIN 25.0f /* command units per output-wheel RPM */
#define BRAKE_DEADZONE_RPM 2.0f
#define CURRENT_LIMIT 16384 /* C620 protocol full-scale, never exceed */
#define SPEED_KP (8.0f*MOTOR_REDUCTION)
#define SPEED_KI (24.0f*MOTOR_REDUCTION)
#define LOOP_SECONDS 0.002f
#if CURRENT_LIMIT < 1 || CURRENT_LIMIT > 16384
#error C620 current command must be within +/-16384
#endif
#if MOTOR_ID_FL<1 || MOTOR_ID_FL>4 || MOTOR_ID_FR<1 || MOTOR_ID_FR>4 || MOTOR_ID_BL<1 || MOTOR_ID_BL>4 || MOTOR_ID_BR<1 || MOTOR_ID_BR>4
#error Four chassis IDs must be 1..4
#endif
#if MOTOR_ID_FL==MOTOR_ID_FR || MOTOR_ID_FL==MOTOR_ID_BL || MOTOR_ID_FL==MOTOR_ID_BR || MOTOR_ID_FR==MOTOR_ID_BL || MOTOR_ID_FR==MOTOR_ID_BR || MOTOR_ID_BL==MOTOR_ID_BR
#error Duplicate chassis ID
#endif
#endif
