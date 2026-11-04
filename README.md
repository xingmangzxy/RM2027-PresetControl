# RM2027 底盘与四舵机控制

基于 RoboMaster C 板和校内赛框架的实验工程，控制 CAN1 底盘及四路 PWM 舵机。

当前假设 SB、SC、SD 分别为 CH5、CH6、CH7，SC 低值停止、中值底盘、高值舵机；SD 低值慢速或预设调整、高值快速或微调。物理开关方向必须通过遥控器监测确认。三个舵机预设尚未标定，切换时不会执行预设动作。

该版本已通过主机逻辑测试和 STM32 编译，尚未烧录或进行实物验证。舵机初始目标可能引起机构移动，停止和失联时 PWM 被撤销，机构可能失去保持力；应在支撑、空载条件下逐步测试。软件过温停机未启用。请核对供电、接线、机械限位和比赛规则。

## 构建

Windows 环境需要 Arm GNU Toolchain、CMake、Ninja、GCC/G++、Python 及 OpenOCD。按实际安装位置调整 `tools/env.ps1`，执行 `powershell -ExecutionPolicy Bypass -File tools/build.ps1`。构建脚本生成 `release/` 和验证记录。

烧录需使用 ST-Link；先断开电调动力并确保舵机机构安全，连接 C 板供电，执行 `powershell -ExecutionPolicy Bypass -File tools/flash.ps1 -MotorPowerDisconnected`。脚本先备份当前固件再写入并校验。构建输出中 `no_actuator_PWM` 为遗留检查字段，不能代表本版本没有 PWM 输出。

代码和依赖保留各自原有许可，请查看 LICENSE 与各依赖目录中的许可文件。

