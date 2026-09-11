# CDC+HID USB远程唤醒验证

## 设备配置

- VID: `0x19F5`
- PID: `0x5740`
- 供电方式: USB总线供电
- 最大声明电流: 100mA
- 接口0/1: CDC ACM虚拟串口
- 接口2: HID Boot Keyboard，仅用于取得Windows唤醒能力，不发送按键
- 唤醒源: PA3或PA7的PIR低到高边沿
- 唤醒保护: USB进入Suspend后，必须确认两路PIR连续保持低电平60秒才布防；期间任一路变高都会清零并重新计时，布防后下一次低到高才允许唤醒

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
3. `PIR_WakeupTimerFromISR()`：确认两路PIR连续低电平60秒后才进入已布防状态。
4. `USB_Remote_Wakeup()`：确认PIR低到高触发时设备处于`SUSPENDED`且授权位为1。
5. `Resume()`的`RESUME_START`分支：确认设置`CTRL_RESUM`。

使用USB分析仪或示波器观察D+/D-，PIR触发后应看到约10ms的Resume信号。

## 实机验证结果

- CDC接口：`USB\VID_19F5&PID_5740&MI_00`，由`usbser.inf`驱动，枚举为COM9。
- HID接口：`HID\VID_19F5&PID_5740&MI_02`，由`keyboard.inf`驱动。
- HID键盘已默认出现在`powercfg /devicequery wake_armed`结果中。
- PC从S3成功唤醒，`powercfg /lastwake`显示唤醒路径经过AMD USB xHCI主控制器。
