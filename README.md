Pet equipment remote control

运行在 esp32 上的奶牛猫电子宠物，支持摸摸、逗猫、叫名互动，并可连接小米宠物喂食器 2 和小佩自动猫砂盆 MAX 2等设备

小猫名字叫 **奶油**。平时它待在小猫窝里探头看你，互动时才会从窝里出来。

> 本项目通过非官方云接口连接小米和小佩设备，仅适合个人学习和实验。请勿提交 Wi-Fi、账号、密码、token 或设备 ID。

## 功能

- 短按正面 A 键：摸摸奶油，手会出现抚摸它，它会蹲起来眯眼享受
- 长按正面 A 键约 3 秒：屏幕提示 `Release A: feed 1p` 后松手投喂；如果接入了小米宠物喂食器 2，会先发送真实出粮请求
- 短按侧边 B 键：逗猫
- 长按侧边 B 键约 1.5 秒：屏幕提示 `Release B: WC info` 后松手查看小佩猫砂盆状态摘要
- 继续长按侧边 B 键到约 5 秒：屏幕提示 `Release B: CLEAN WC` 后松手，手动启动小佩猫砂盆清理
- 对着麦克风叫「奶油」：奶油会听到，先从窝里出来，然后开心转圈
- 可选：接入小米宠物喂食器 2 后，长按 A 键约 3 秒会先向喂食器发送出粮请求，成功后奶油再播放吃饭动画
- 待机画面：淡米色背景，小猫窝里只露出奶油的脑袋
- 互动画面：奶油会先在猫窝门口露头伸爪，再滑出来显示一只更小、更圆的侧躺奶牛猫

当前语音触发是轻量版：程序会先校准环境音，再检测明显的人声，把它当作在叫「奶油」。这适合先把互动跑起来；如果后面要真的识别「奶油」两个字，可以再接 ESP-SR 或联网语音识别。

## 用 Arduino IDE 烧录

1. 安装 Arduino IDE。
2. 在 Boards Manager 安装 `esp32` 开发板支持。
3. 在 Library Manager 安装 `M5StickCPlus`。
4. 在 Library Manager 安装 `ArduinoJson`。
5. 打开 `CreamCat/CreamCat.ino`。
6. 开发板选择 `M5Stick-C` 或对应的 M5StickC Plus ESP32 选项。
7. 插上 M5StickC Plus，选择串口，点击上传。

## 用 PlatformIO 烧录

项目已经带了 `platformio.ini`。安装 PlatformIO 后，在本目录执行：

```bash
pio run -t upload
```

串口监视：

```bash
pio device monitor
```

## 小米宠物喂食器 2 配置

小米喂食器接入是可选功能。默认是关闭的，所以不填配置也可以继续玩普通奶油猫咪。

如果要开启：

1. 复制 `CreamCat/CreamCatSecrets.example.h`，改名为 `CreamCat/CreamCatSecrets.h`。
2. 在 `CreamCatSecrets.h` 里填写你的配置。
3. 把 `CREAMCAT_XIAOMI_FEEDER_ENABLED` 改成 `1`。
4. 重新上传程序到 M5。

需要填写的值：

```cpp
#define CREAMCAT_WIFI_SSID "你的2.4G Wi-Fi名称"
#define CREAMCAT_WIFI_PASSWORD "你的Wi-Fi密码"

#define CREAMCAT_XIAOMI_FEEDER_ENABLED 1
#define CREAMCAT_XIAOMI_REGION "cn"
#define CREAMCAT_XIAOMI_USER_ID "你的小米账号userId"
#define CREAMCAT_XIAOMI_SERVICE_TOKEN "小米serviceToken"
#define CREAMCAT_XIAOMI_SSECURITY "小米ssecurity"
#define CREAMCAT_XIAOMI_DEVICE_ID "喂食器did"
#define CREAMCAT_XIAOMI_DEVICE_MODEL "xiaomi.feeder.pi2001"

#define CREAMCAT_FEEDER_DEFAULT_PORTIONS 1
```

安全提醒：`CreamCatSecrets.h` 里会放 Wi-Fi 密码和云端凭据，不要上传 GitHub。项目里的 `.gitignore` 已经忽略了这个文件。

当前代码使用小米云 MIoT 的非官方调用方式：

- 读取喂食器状态：余粮、卡粮、食盆异常、食盆余粮克数、出粮状态
- 发放食物：只调用一次 `pet-food-out`，默认 `1` 份。出粮前会先读取设备状态；如果称重故障、卡粮、出粮异常、食盆堆积或设备正在出粮，就不会发送出粮命令。

按键安全设计：真实出粮只会在 A 键长按达到 3 秒并且松手后触发一次。按住 A 键本身不会连续发送出粮命令；再次长按会重新检查设备状态后再发送。

这套方式适合自己玩和实验，不是小米官方给个人项目承诺长期稳定的公开 API。如果小米接口或 token 机制变化，可能需要重新调试。

## M5 上的小佩猫砂盆显示

小佩配置开启后，屏幕左侧会出现一个小猫砂盆图标：

- `--`：还没同步到状态
- 百分比：当前猫砂量，例如 `85%`
- `!`：集便仓满、猫砂不足、低电量、宠物异常或设备错误
- 黄灯/扫动线：猫砂盆正在工作或状态不是完全空闲

底部文字会和小米喂食器状态轮播：

- `Litter: sand 85% used 12`：猫砂量 85%，今日使用 12 次
- `Litter: bin full`：集便仓满
- `Litter: low sand`：猫砂不足
- `Litter: offline`：设备离线
- `Litter: error`：设备返回异常

当前 M5 端小佩功能支持手动清理，但不支持倒砂。手动清理需要长按 B 键 5 秒并松手确认；执行前会先刷新设备状态，如果设备离线、童锁开启、集便仓满、猫砂不足、低电量、宠物异常、设备报错或正在工作，就不会发送清理命令。

## 小佩自动猫砂盆 MAX 2 准备

小佩接入先做只读探测，不直接控制电机。

1. 建议新建一个 PETKIT 小佩小号，并在小佩 App 里把猫砂盆共享给小号。
2. 复制 `tools/petkit/PetkitSecrets.example.json`，改名为 `tools/petkit/PetkitSecrets.json`。
3. 填入小佩账号、密码、地区和时区：

```json
{
  "username": "your-petkit-email-or-phone",
  "password": "your-petkit-password",
  "region": "CN",
  "timezone": "Asia/Shanghai"
}
```

4. 在项目目录运行只读探测：

```bash
tools/petkit/.venv/bin/python tools/petkit/read_petkit_litter.py
```

脚本会列出猫砂盆名称、`device_id`、设备类型、固件、集便仓、猫砂不足、异常信息、工作状态等。确认可以稳定读到 MAX 2 后，再把 `device_id` 填到 `CreamCat/CreamCatSecrets.h` 里的 `CREAMCAT_PETKIT_DEVICE_ID`。

安全提醒：猫砂盆涉及电机和猫咪安全，当前只开放“长按确认后手动清理”，不开放倒砂。第一次测试时请人在旁边观察，确认猫咪不在猫砂盆里、设备周围没有卡住的东西。

## 麦克风说明

M5StickC Plus 的内置 PDM 麦克风使用：

- CLK: GPIO0
- DATA: GPIO34

程序启动后会先用约 1.4 秒估计环境底噪。开机时尽量先别说话，等屏幕不再显示 `Mic calibrating...` 后，再让设备离嘴近一点，清楚喊「奶油」。

底部右侧会显示麦克风状态：

- `CAL`：正在校准环境音
- `V012` / `V120`：麦克风读到的声音强度，数字越大代表声音越明显
- `E1` / `E2`：麦克风读取出错

如果你说话时 `V` 数字明显变大，说明麦克风能听到，只是触发阈值还要继续调。如果 `V` 数字完全不动，或者一直显示 `E`，说明麦克风采集没有成功，需要先检查板子型号和端口/库版本。

## 后续可升级

- 增加睡觉、生病、撒娇等状态
- 增加红外遥控动作或蜂鸣器音效
- 把叫名检测升级成真正关键词识别
- 保存状态到 flash，断电后继续养
