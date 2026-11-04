#include "main.h"
#include "can.h"
#include "diagnostic.h"
#include "control.h"

static Diagnostic diag;
static Controller control;
static uint32_t pending,pending_since,tx_count,tx_errors;
static uint32_t can_stage,can_stage_since,can_retry_since,can_retry_seen;
static uint32_t can_restart_needed,can_restart_count,can_saved_btr,can_saved_mcr;
/* The host reads this seqlock snapshot, never writes targets or halts the core. */
volatile uint32_t g_snapshot[DIAG_WORDS];
__attribute__((used,section(".buildinfo"))) const uint32_t g_diag_identity[]={DIAG_MAGIC,DIAG_VERSION,DIAG_WORDS,1};
__attribute__((used,section(".buildinfo"))) const uint32_t g_temperature_stop_enabled=TEMPERATURE_STOP_ENABLED;
__attribute__((used,section(".buildinfo"))) const uint32_t g_control_logic_revision=CONTROL_LOGIC_REVISION;
__attribute__((used,section(".buildinfo"))) const uint32_t g_ground_profile=GROUND_TEST_PROFILE;
__attribute__((used,section(".buildinfo"))) const uint32_t g_direct_start=DIRECT_START;
extern volatile unsigned servo_pulse_us, servo2_pulse_us, servo3_pulse_us, servo4_pulse_us;
static UART_HandleTypeDef uart;
static DMA_HandleTypeDef dma;
static IWDG_HandleTypeDef watchdog;
static volatile uint8_t ring[256];
static uint32_t tail;

void _init(void) {}
void _fini(void) {}
void SysTick_Handler(void) {HAL_IncTick();}
void HAL_MspInit(void) {__HAL_RCC_SYSCFG_CLK_ENABLE();__HAL_RCC_PWR_CLK_ENABLE();}
static void fatal(void) {
    if(hcan1.Instance)hcan1.Instance->TSR=CAN_TSR_ABRQ0|CAN_TSR_ABRQ1|CAN_TSR_ABRQ2;
    if(hcan2.Instance)hcan2.Instance->TSR=CAN_TSR_ABRQ0|CAN_TSR_ABRQ1|CAN_TSR_ABRQ2;
    __disable_irq();for(;;){}
}
void Error_Handler(void) {fatal();}
void NMI_Handler(void) {fatal();}
void HardFault_Handler(void) {fatal();}
void MemManage_Handler(void) {fatal();}
void BusFault_Handler(void) {fatal();}
void UsageFault_Handler(void) {fatal();}
void SystemClock_Config(void) {
    RCC_OscInitTypeDef o={0}; RCC_ClkInitTypeDef c={0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    o.OscillatorType=RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSI;
    o.HSEState=RCC_HSE_ON;o.LSIState=RCC_LSI_ON;
    o.PLL.PLLState=RCC_PLL_ON;o.PLL.PLLSource=RCC_PLLSOURCE_HSE;
    o.PLL.PLLM=6;o.PLL.PLLN=168;o.PLL.PLLP=RCC_PLLP_DIV2;o.PLL.PLLQ=7;
    if(HAL_RCC_OscConfig(&o)!=HAL_OK)fatal();
    c.ClockType=RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    c.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK;c.AHBCLKDivider=RCC_SYSCLK_DIV1;
    c.APB1CLKDivider=RCC_HCLK_DIV4;c.APB2CLKDivider=RCC_HCLK_DIV2;
    if(HAL_RCC_ClockConfig(&c,FLASH_LATENCY_5)!=HAL_OK)fatal();
    HAL_RCC_EnableCSS();
}
static void leds_init(void) {
    __HAL_RCC_GPIOH_CLK_ENABLE();__HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOH,GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12,GPIO_PIN_RESET);
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
    g.Mode=GPIO_MODE_OUTPUT_PP;g.Speed=GPIO_SPEED_FREQ_LOW;HAL_GPIO_Init(GPIOH,&g);
    g.Pin=GPIO_PIN_0;g.Mode=GPIO_MODE_INPUT;g.Pull=GPIO_PULLUP;HAL_GPIO_Init(GPIOA,&g);
}
void HAL_UART_MspInit(UART_HandleTypeDef *u) {
    if(u->Instance!=USART3)return;
    __HAL_RCC_USART3_CLK_ENABLE();__HAL_RCC_GPIOC_CLK_ENABLE();__HAL_RCC_DMA1_CLK_ENABLE();
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_11;g.Mode=GPIO_MODE_AF_PP;
    g.Pull=GPIO_PULLUP;g.Speed=GPIO_SPEED_FREQ_VERY_HIGH;g.Alternate=GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOC,&g);
}
static void servo_pwm_init(void) {
    __HAL_RCC_GPIOE_CLK_ENABLE();__HAL_RCC_TIM1_CLK_ENABLE();
    GPIO_InitTypeDef g={0};g.Pin=GPIO_PIN_9|GPIO_PIN_11|GPIO_PIN_13|GPIO_PIN_14;g.Mode=GPIO_MODE_AF_PP;
    g.Pull=GPIO_NOPULL;g.Speed=GPIO_SPEED_FREQ_LOW;g.Alternate=GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOE,&g);
    TIM1->PSC=167;TIM1->ARR=19999;TIM1->CCR1=0;TIM1->CCR2=0;TIM1->CCR3=0;TIM1->CCR4=0;
    TIM1->CCMR1=TIM_CCMR1_OC1PE | (6u<<4) | TIM_CCMR1_OC2PE | (6u<<12);TIM1->CCMR2=TIM_CCMR2_OC3PE | (6u<<4) | TIM_CCMR2_OC4PE | (6u<<12);TIM1->CCER=TIM_CCER_CC1E|TIM_CCER_CC2E|TIM_CCER_CC3E|TIM_CCER_CC4E;
    TIM1->BDTR=TIM_BDTR_MOE;TIM1->EGR=TIM_EGR_UG;TIM1->CR1=TIM_CR1_ARPE|TIM_CR1_CEN;
}
static void remote_init(void) {
    uart.Instance=USART3;uart.Init.BaudRate=100000;
    uart.Init.WordLength=UART_WORDLENGTH_9B;uart.Init.Parity=UART_PARITY_EVEN;
    uart.Init.StopBits=UART_STOPBITS_2;uart.Init.Mode=UART_MODE_RX;
    uart.Init.HwFlowCtl=UART_HWCONTROL_NONE;uart.Init.OverSampling=UART_OVERSAMPLING_16;
    if(HAL_UART_Init(&uart)!=HAL_OK)fatal();
    dma.Instance=DMA1_Stream1;dma.Init.Channel=DMA_CHANNEL_4;
    dma.Init.Direction=DMA_PERIPH_TO_MEMORY;dma.Init.MemInc=DMA_MINC_ENABLE;
    dma.Init.PeriphInc=DMA_PINC_DISABLE;dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE;
    dma.Init.MemDataAlignment=DMA_MDATAALIGN_BYTE;dma.Init.Mode=DMA_CIRCULAR;
    dma.Init.Priority=DMA_PRIORITY_HIGH;dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(&dma)!=HAL_OK)fatal();
    if(HAL_DMA_Start(&dma,(uint32_t)&USART3->DR,(uint32_t)ring,sizeof(ring))!=HAL_OK)fatal();
    SET_BIT(USART3->CR3,USART_CR3_DMAR);
}
static void remote_poll(uint32_t now) {
    if(USART3->SR & (USART_SR_PE|USART_SR_FE|USART_SR_NE|USART_SR_ORE)) {
        volatile uint32_t discard=USART3->DR;(void)discard;
        diag_uart_error(&diag);rc_uart_error(&control);tail=(256-DMA1_Stream1->NDTR)&255;
    }
    if(DMA1->LISR & (DMA_LISR_TEIF1|DMA_LISR_DMEIF1|DMA_LISR_FEIF1)) {
        diag_uart_error(&diag);rc_uart_error(&control);fatal();
    }
    uint32_t head=(256-DMA1_Stream1->NDTR)&255;
    __DMB();
    for(unsigned n=0;n<256 && tail!=head;++n) {
        uint8_t value=ring[tail];tail=(tail+1)&255;
        diag_byte(&diag,now,value);rc_byte(&control,now,value);
    }
}
static int can_filter(unsigned i) {
    CAN_FilterTypeDef f={0};f.FilterMode=CAN_FILTERMODE_IDMASK;
    f.FilterScale=CAN_FILTERSCALE_32BIT;f.FilterActivation=ENABLE;
    f.FilterFIFOAssignment=CAN_RX_FIFO0;f.SlaveStartFilterBank=14;f.FilterBank=i ? 14 : 0;
    return HAL_CAN_ConfigFilter(i ? &hcan2 : &hcan1,&f)==HAL_OK;
}
static void can_init(void) {
    MX_CAN1_Init();MX_CAN2_Init();
    HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);HAL_NVIC_DisableIRQ(CAN2_RX0_IRQn);
    for(unsigned i=0;i<2;++i)
        if(!can_filter(i) || HAL_CAN_Start(i ? &hcan2 : &hcan1)!=HAL_OK)fatal();
    can_saved_btr=CAN1->BTR;
    can_saved_mcr=CAN1->MCR & (CAN_MCR_TTCM|CAN_MCR_ABOM|CAN_MCR_AWUM|
        CAN_MCR_NART|CAN_MCR_RFLM|CAN_MCR_TXFP|CAN_MCR_DBF);
}
static void can_problem(uint32_t now,unsigned issue) {
    rc_can_error(&control,now,issue,CAN1->ESR);
    CAN1->TSR=CAN_TSR_ABRQ0|CAN_TSR_ABRQ1|CAN_TSR_ABRQ2;pending=0;
    can_restart_needed=1;
}
/* No blocking HAL stop/start loops during recovery: a stuck dominant bus must
 * not stall the radio/control task or starve the independent watchdog. GPIO,
 * clocks and CAN2 are kept configured; shared acceptance filters are restored. */
static void can_recovery_service(uint32_t now) {
    if(!can_stage) {
        if(!can_restart_needed || (can_retry_seen && now-can_retry_since<CAN_RETRY_MS))return;
        can_retry_seen=1;can_retry_since=now;++can_restart_count;
        control.can_hw_ok=0;
        CAN1->TSR=CAN_TSR_ABRQ0|CAN_TSR_ABRQ1|CAN_TSR_ABRQ2;pending=0;
        CAN1->MCR|=CAN_MCR_RESET;can_stage=1;can_stage_since=now;return;
    }
    if(now-can_stage_since>=CAN_STAGE_TIMEOUT_MS) {
        rc_can_error(&control,now,CAN_ISSUE_RESTART,CAN1->ESR);
        can_stage=0;can_restart_needed=1;return;
    }
    if(can_stage==1 && !(CAN1->MCR & CAN_MCR_RESET)) {
        hcan1.State=HAL_CAN_STATE_READY;hcan1.ErrorCode=HAL_CAN_ERROR_NONE;
        CAN1->IER=0;CAN1->MCR=can_saved_mcr|CAN_MCR_INRQ;
        can_stage=2;can_stage_since=now;return;
    }
    if(can_stage==2 && (CAN1->MSR & CAN_MSR_INAK)) {
        CAN1->BTR=can_saved_btr;
        if(!can_filter(0) || !can_filter(1)) {
            rc_can_error(&control,now,CAN_ISSUE_RESTART,CAN1->ESR);
            can_stage=0;can_restart_needed=1;return;
        }
        CAN1->MCR=can_saved_mcr;can_stage=3;can_stage_since=now;return;
    }
    if(can_stage==3 && !(CAN1->MSR & CAN_MSR_INAK)) {
        hcan1.State=HAL_CAN_STATE_LISTENING;hcan1.ErrorCode=HAL_CAN_ERROR_NONE;
        control.can_hw_ok=1;can_stage=0;can_restart_needed=0;
    }
}
static void can_poll(unsigned i,uint32_t now) {
    CAN_HandleTypeDef *h=i ? &hcan2 : &hcan1;unsigned b=D_BUS+i*16;
    if(!i && can_stage)return;
    if(h->Instance->RF0R & CAN_RF0R_FOVR0) {
        ++diag.data[b+7];__HAL_CAN_CLEAR_FLAG(h,CAN_FLAG_FOV0);
        if(!i)can_problem(now,CAN_ISSUE_FIFO);
    }
    for(unsigned n=0;n<16 && HAL_CAN_GetRxFifoFillLevel(h,CAN_RX_FIFO0);++n) {
        CAN_RxHeaderTypeDef header={0};uint8_t bytes[8]={0};
        if(HAL_CAN_GetRxMessage(h,CAN_RX_FIFO0,&header,bytes)!=HAL_OK){
            ++diag.data[b+10];if(!i)can_problem(now,CAN_ISSUE_RX);break;
        }
        diag_can(&diag,now,i,header.IDE==CAN_ID_STD ? header.StdId : header.ExtId,
                 header.IDE!=CAN_ID_STD,header.RTR!=CAN_RTR_DATA,header.DLC,bytes);
        if(!i) {
            if(header.IDE==CAN_ID_STD && header.RTR==CAN_RTR_DATA &&
               (header.StdId==0x200 || header.StdId==0x1ff)) rc_fault(&control,F_CAN_CONFLICT);
            rc_motor(&control,now,header.IDE==CAN_ID_STD ? header.StdId : header.ExtId,
                     header.IDE!=CAN_ID_STD,header.RTR!=CAN_RTR_DATA,header.DLC,bytes);
        }
    }
    diag.data[b+8]=h->Instance->ESR;
    if(!i && (h->Instance->ESR & (CAN_ESR_BOFF|CAN_ESR_EPVF)))
        can_problem(now,(h->Instance->ESR & CAN_ESR_BOFF) ? CAN_ISSUE_BUSOFF : CAN_ISSUE_PASSIVE);
}
static int active(void) {return control.state==RC_ARMED_BENCH || control.state==RC_ARMED_DRIVE;}
void BoardTransmit(uint32_t now) {
    /* No fresh ESC => no endless zero frames on an unpowered/unplugged bus. */
    int any_fresh=0;
    for(unsigned i=0;i<4;++i)
        if(control.motor[i].count && now-control.motor[i].stamp<=MOTOR_TIMEOUT_MS)any_fresh=1;
    if(can_stage || can_restart_needed || !control.can_hw_ok || !any_fresh) {
        if(pending)CAN1->TSR=CAN_TSR_ABRQ0|CAN_TSR_ABRQ1|CAN_TSR_ABRQ2;
        pending=0;return;
    }
    if(pending && HAL_CAN_IsTxMessagePending(&hcan1,pending) && (!active() || control.fault)) {
        HAL_CAN_AbortTxRequest(&hcan1,pending);pending=0;
    }
    if(pending && HAL_CAN_IsTxMessagePending(&hcan1,pending)) {
        if(now-pending_since>4) {
            HAL_CAN_AbortTxRequest(&hcan1,pending);pending=0;
            can_problem(now,CAN_ISSUE_TX);++tx_errors;return;
        } else return;
    } else if(pending) {
        uint32_t ok=pending==CAN_TX_MAILBOX0 ? CAN_TSR_TXOK0 :
                    pending==CAN_TX_MAILBOX1 ? CAN_TSR_TXOK1 : CAN_TSR_TXOK2;
        if(!(hcan1.Instance->TSR & ok)){can_problem(now,CAN_ISSUE_TX);++tx_errors;return;}
        pending=0;
    }
    if(!HAL_CAN_GetTxMailboxesFreeLevel(&hcan1)){can_problem(now,CAN_ISSUE_TX);++tx_errors;return;}
    uint8_t bytes[8];rc_pack(&control,bytes);
    hcan1.Instance->TSR=CAN_TSR_RQCP0|CAN_TSR_RQCP1|CAN_TSR_RQCP2;
    CAN_TxHeaderTypeDef header={0};header.StdId=0x200;header.IDE=CAN_ID_STD;
    header.RTR=CAN_RTR_DATA;header.DLC=8;uint32_t mailbox=0;
    if(HAL_CAN_AddTxMessage(&hcan1,&header,bytes,&mailbox)!=HAL_OK){can_problem(now,CAN_ISSUE_TX);++tx_errors;}
    else {pending=mailbox;pending_since=now;++tx_count;}
}
static void drive_snapshot(void) {
    diag.data[208]=control.state;diag.data[209]=control.fault;diag.data[210]=control.reason;
    static const unsigned ids[]={MOTOR_ID_FL,MOTOR_ID_FR,MOTOR_ID_BL,MOTOR_ID_BR};
    diag.data[211]=0;diag.data[212]=0;
    diag.data[213]=control.geometry_confirmed;diag.data[214]=control.ready;
    diag.data[215]=tx_count;diag.data[216]=tx_errors;diag.data[217]=0;
    for(unsigned i=0;i<4;++i) {
        unsigned slot=ids[i]-1;
        diag.data[218+slot]=(uint32_t)(int32_t)control.output[i];
        diag.data[222+slot]=(uint32_t)(int32_t)control.target[i];
        if(control.motor[i].count && control.motor[i].temperature>0)diag.data[217]|=1u<<slot;
    }
    diag.data[226]=HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_0)==GPIO_PIN_RESET;
    diag.data[227]=control.radio.channel[4];diag.data[228]=control.radio.channel[5];
    diag.data[229]=control.radio.healthy;diag.data[230]=control.radio.streak;
    diag.data[231]=TEMPERATURE_STOP_ENABLED;
    diag.data[232]=control.arm_wait_ms;diag.data[233]=control.motion_started;
    diag.data[234]=control.test_remaining_ms;diag.data[236]=CONTROL_LOGIC_REVISION;
    diag.data[237]=GROUND_TEST_PROFILE;diag.data[238]=control.confirm_wait_ms;
    diag.data[239]=control.fault_motor_id;
    diag.data[240]=(uint32_t)(int32_t)control.fault_rpm;
    diag.data[241]=(uint32_t)(int32_t)control.fault_current;
    diag.data[242]=control.fault_temperature;
    diag.data[243]=CURRENT_LIMIT;diag.data[244]=(uint32_t)(NORMAL_LINEAR_MPS*60.0f/(3.14159265f*WHEEL_DIAMETER_M)*MOTOR_REDUCTION);
    diag.data[245]=0; /* Continuous normal operation, no per-test time limit. */
    diag.data[246]=control.radio.channel[4]>=1400 ? 2 : 1;
    diag.data[247]=(uint32_t)(NORMAL_LINEAR_MPS*1000);
    diag.data[248]=DIRECT_START;diag.data[249]=(uint32_t)(NORMAL_YAW_RPM*1000);
    diag.data[250]=control.can_recovering;diag.data[251]=control.can_incidents;
    diag.data[252]=can_restart_count;diag.data[253]=control.can_last_esr;
    diag.data[254]=control.can_last_issue;diag.data[255]=control.can_wait_ms;
}
static void publish(void) {
    uint32_t sequence=g_snapshot[D_SEQUENCE]+1;
    g_snapshot[D_SEQUENCE]=sequence;__DMB();
    for(unsigned i=0;i<DIAG_WORDS;++i)if(i!=D_SEQUENCE)g_snapshot[i]=diag.data[i];
    __DMB();g_snapshot[D_SEQUENCE]=sequence+1;
}
int main(void) {
    HAL_Init();__HAL_DBGMCU_UNFREEZE_IWDG();
    diag_init(&diag);diag.data[D_RESET]=RCC->CSR;__HAL_RCC_CLEAR_RESET_FLAGS();
    rc_init(&control,HAL_GetTick(),1);FrameInit(&control);
    watchdog.Instance=IWDG;watchdog.Init.Prescaler=IWDG_PRESCALER_32;watchdog.Init.Reload=99;
    if(HAL_IWDG_Init(&watchdog)!=HAL_OK)fatal();
    SystemClock_Config();leds_init();remote_init();can_init();servo_pwm_init();
    uint32_t last=HAL_GetTick(),last_publish=last;
    control.last_step=last;drive_snapshot();publish();
    for(;;) {
        uint32_t now=HAL_GetTick();remote_poll(now);can_poll(0,now);can_poll(1,now);can_recovery_service(now);
        if(now-last>=2) {
            last=now;++diag.data[D_LOOPS];diag_tick(&diag,now);
            rc_step(&control,now,HAL_GPIO_ReadPin(GPIOA,GPIO_PIN_0)==GPIO_PIN_RESET);
            TIM1->CCR1=servo_pulse_us;TIM1->CCR2=servo2_pulse_us;TIM1->CCR3=servo3_pulse_us;TIM1->CCR4=servo4_pulse_us;
            if(now-last_publish>=250){last_publish=now;drive_snapshot();publish();}
            HAL_GPIO_WritePin(GPIOH,GPIO_PIN_10,(now%1000)<500 ? GPIO_PIN_SET : GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOH,GPIO_PIN_11,active() ? GPIO_PIN_SET : GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOH,GPIO_PIN_12,(control.fault || (control.can_recovering && (now%500)<250)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            HAL_IWDG_Refresh(&watchdog);
        }
    }
}
