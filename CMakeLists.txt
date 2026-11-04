cmake_minimum_required(VERSION 3.20)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(RM2027_MecanumFast C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(HAL ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver)
set(SOURCES userCode/runtime/BoardMain.c userCode/diagnostic/RemoteDiagnostic.c userCode/runtime/control.c
    userCode/tasks/Src/ChassisTask.cpp userCode/tasks/Src/ControlTask.cpp userCode/tasks/Src/UserTask.cpp
    userCode/devices/Src/Motor.cpp userCode/devices/Src/RemoteControl.cpp
    Src/can.c Src/system_stm32f4xx.c startup/startup_stm32f407xx.s)
foreach(part stm32f4xx_hal stm32f4xx_hal_rcc stm32f4xx_hal_rcc_ex
    stm32f4xx_hal_gpio stm32f4xx_hal_cortex stm32f4xx_hal_can stm32f4xx_hal_uart stm32f4xx_hal_dma
    stm32f4xx_hal_iwdg stm32f4xx_hal_flash stm32f4xx_hal_flash_ex
    stm32f4xx_hal_pwr stm32f4xx_hal_pwr_ex)
  list(APPEND SOURCES ${HAL}/Src/${part}.c)
endforeach()
set(IMAGE CBOARD_MECANUM_FAST)
add_executable(${IMAGE}.elf ${SOURCES})
target_include_directories(${IMAGE}.elf PRIVATE Inc userCode/diagnostic userCode/runtime userCode/devices/Inc userCode/tasks/Inc ${HAL}/Inc
  Drivers/CMSIS/Include Drivers/CMSIS/Device/ST/STM32F4xx/Include)
target_compile_definitions(${IMAGE}.elf PRIVATE USE_HAL_DRIVER STM32F407xx)
target_compile_options(${IMAGE}.elf PRIVATE -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
  -O2 -g3 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -Wno-unused-parameter)
target_compile_options(${IMAGE}.elf PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-fno-use-cxa-atexit>)
target_link_options(${IMAGE}.elf PRIVATE -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
  -nostartfiles --specs=nano.specs --specs=nosys.specs
  -T${CMAKE_CURRENT_SOURCE_DIR}/userCode/diagnostic/stm32f407.ld
  -Wl,--fatal-warnings,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/${IMAGE}.map,--print-memory-usage)
add_custom_command(TARGET ${IMAGE}.elf POST_BUILD
  COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${IMAGE}.elf> ${IMAGE}.hex
  COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${IMAGE}.elf> ${IMAGE}.bin
  COMMAND arm-none-eabi-size $<TARGET_FILE:${IMAGE}.elf>)
