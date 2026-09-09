# 红外重复发送回归测试

从当前 `bsp_ir.c` 和 `gbe_protocol.c` 提取发送函数、TIM6状态机和请求处理函数，用 Keil ARMCC 编译后在 Unicorn ARM 仿真器中执行。仅替换定时器、时钟和USB操作，不复制一套重发算法。测试不连接开发板，也不验证真实GPIO、载波或USB硬件。

在工程根目录运行（Python需支持pip，Keil路径按本机调整）：

```powershell
python -m pip install --target MDK-ARM/Objects/ir_repeat_test/python_deps unicorn==2.1.4
python tests/run_ir_repeat.py --keil D:/software/Keil_MDK
```

依赖和生成的测试程序都保存在已忽略的 `MDK-ARM/Objects/ir_repeat_test` 中，不加入固件。已有有效FVP许可证时可增加 `--fvp`，不依赖Unicorn。

覆盖1/3/255次、NEC特殊重复帧、Sony 12/15/20位、AEHA最长1280位、16位毫秒计数回绕、最小静默间隔、Busy期间缓存保护、响应提交失败后的重试、非法请求和单帧超时后的恢复。成功时输出 `IR REPEAT TESTS PASSED`；输出的checks数字是断言执行次数，不是测试场景数。

固件参考周期集中在 `bsp_ir.c`：NEC 108ms、Sony 45ms，都是帧起始到起始的间隔，可对照 [NEC参考实现中的IRP描述](https://github.com/Arduino-IRremote/Arduino-IRremote/blob/master/src/ir_NEC.hpp) 和 [Sony参考实现](https://github.com/Arduino-IRremote/Arduino-IRremote/blob/master/src/ir_Sony.hpp)。AEHA沿用当前调试方案的130ms参考周期，并单独保留至少8ms静默时间；长帧会延后下一次发送。这些配置仍需用目标家电和逻辑分析仪确认。

主循环延迟可能拉长重发周期。Repeat Count仍表示总次数1~255，长序列可能超过通信文档中的PC侧10秒响应超时；本实现不把次数静默截短。单物理帧3秒未完成会关闭输出并返回Internal Error。
