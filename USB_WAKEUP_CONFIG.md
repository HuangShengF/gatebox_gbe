# PIR与CDC+HID USB远程唤醒验证

## 设备配置

- VID: `0x19F5`
- PID: `0x5740`
- 供电方式: USB总线供电
- 最大声明电流: 100mA
- 接口0/1: CDC ACM虚拟串口
- 接口2: HID Boot Keyboard，仅用于取得Windows唤醒能力，不发送按键
- 唤醒源: PA3（左）或PA7（右），左右方向仍需结合原理图/实机确认。
- 唤醒开关: 上电/MCU复位默认关闭，仅保存在RAM；重握手、COM开关、USB总线复位和不断电重连均保留配置。
- 唤醒保护: 开关启用且USB进入Suspend后，两路连续LOW至少10秒才布防；布防后任一路独立连续HIGH至少1秒才请求唤醒。短HIGH不能累计，左右HIGH不能拼接。
- 单次限制: 同一睡眠周期最多请求一次；切换开关、重握手和本机WKUP不解除锁存，收到主机SOF后才允许下一轮睡眠再次唤醒。

## Rev2.4开关命令

需先完成Handshake，继续使用协议版本1.0及原有帧结构。

| 操作 | Request | 请求Payload | Response | 响应Payload |
| --- | --- | --- | --- | --- |
| Set | `0x0501` | 1字节：`00`关闭、`01`启用 | `0x1501` | 空 |
| Get | `0x0502` | 空 | `0x1502` | 1字节当前开关值 |

错误命令分别为`0x3501`、`0x3502`。未握手返回`Invalid State`；长度错误返回`Invalid Payload`（Detail分别为1、0）；Set值大于1返回`Invalid Param`（Detail=0）；PIR未初始化返回`Internal Error`。错误请求不改变配置。

相同Set值重发不清计时、不重新布防；Get无副作用。0→1时若已经挂起，从此时重新开始完整静默检测。关闭只禁止PIR唤醒，`0x0303`状态查询和`0x2302`运动通知继续工作。

主机响应超时为500ms，允许使用原Sequence和Payload重试；Set先应用再应答。响应未能进入现有USB发送队列时，主机重试相同值不会重置检测进度。

烧录前先在设备管理器中卸载原来的`VID_19F5&PID_5740`设备，烧录后重新插拔USB，避免Windows继续使用原纯CDC设备的驱动绑定缓存。正式产品应使用已分配的VID/PID。

## Windows检查

```powershell
powercfg /a
powercfg /devicequery wake_programmable
powercfg /devicequery wake_armed
```

本机实测HID键盘枚举后已默认加入`wake_armed`，无需手动设置“允许此设备唤醒计算机”。其他Windows主机如果未自动启用，可在设备管理器中根据“位置路径”找到本设备对应的HID键盘并启用唤醒，同时检查BIOS/UEFI中的USB Wake选项以及所用USB端口是否支持从S3唤醒。

不要通过修改所有USB根集线器注册表项来代替设备唤醒授权。Windows必须为本设备建立Wait/Wake请求，并在挂起前发送`SET_FEATURE(DEVICE_REMOTE_WAKEUP)`。

## 固件检查点

1. `Standard_SetDeviceFeature()`：确认Windows已授权Remote Wakeup。
2. `Suspend()`：确认PC睡眠后USB总线进入Suspend。
3. `PIR_WakeupTimerFromISR()`：观察`g_pir_wakeup_state`、`g_pir_quiet_ticks`和左右`high_ticks`；确认连续LOW至少10秒后布防，连续HIGH至少1秒才请求唤醒。
4. `USB_Remote_Wakeup()`：确认设备处于`SUSPENDED`、开关启用且主机授权位为1；观察`g_pir_wakeup_sent`只锁存一次。
5. `Resume()`的`RESUME_START`分支：确认设置`CTRL_RESUM`。
6. `SOF_Callback()` → `PIR_WakeupOnSof()`：确认真实收到主机SOF后才解除已唤醒锁存。

使用USB分析仪或示波器观察D+/D-，PIR触发后应看到约10ms的Resume信号。

TIM4沿用原定时器，改为10ms采样，只有启用且挂起检测期间运行。首次LOW/HIGH采样用于确定计时起点；按正常中断节拍，阈值检测会增加最多约一个采样周期的延迟。EXTI双边沿负责打断连续计时，避免采样间的短脉冲被累计。阈值附近的实际时间和中断延迟仍需逻辑分析仪确认。

## 本次验证与待测项

- Keil工程编译通过：0错误、6个原有未使用变量警告。
- 使用实际PIR状态机、协议解析和USB回调代码做ARM仿真，129项断言通过；GPIO、定时器和USB硬件由桩替代，不代表实板验证。
- 实板输入建议：LOW 9.99/10.01秒；HIGH 0.99/1.01秒；左右交替各0.6秒；采样间短HIGH/LOW；计时中重复Set/Get；关闭后重新启用。
- 验证主机未授权时不唤醒；已请求但主机未恢复时，重复Suspend、开关切换及USB复位均不再请求；真实恢复后下一次睡眠可再次请求。
- 验证默认关闭时状态查询/通知正常，以及重握手、COM重开、不断电重连保留开关；断电/MCU复位恢复关闭。
- 当前硬件接口只有两路GPIO电平，没有传感器健康状态；输入下拉导致传感器断线可能表现为LOW，不能据此确认传感器可用。如需故障抑制，需要补充硬件诊断依据。
- 固件以USB Suspend/SOF作为睡眠/恢复依据；USB选择性挂起与PC睡眠的对应关系，以及S3/S0低功耗等模式，需在目标主机验证。

## 既有USB唤醒链路实机记录

以下为此前实现的记录，本次Rev2.4状态机尚未重新进行实机唤醒验证。

- CDC接口：`USB\VID_19F5&PID_5740&MI_00`，由`usbser.inf`驱动，枚举为COM9。
- HID接口：`HID\VID_19F5&PID_5740&MI_02`，由`keyboard.inf`驱动。
- HID键盘已默认出现在`powercfg /devicequery wake_armed`结果中。
- PC从S3成功唤醒，`powercfg /lastwake`显示唤醒路径经过AMD USB xHCI主控制器。
