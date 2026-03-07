# Thingino-Button

`thingino-button` 是一个简单的程序，用于监控指定设备的输入事件并根据基于步骤的配置执行相应的命令。它主要针对嵌入式系统，在这些系统中需要通过在引导式工作流中按下按钮来触发特定操作。

## 功能

- 监控输入设备的按键事件
- 基于步骤的工作流系统，支持可配置的步骤
- 在按下 KEY_ENTER 或 KEY_1 时执行不同的命令
- 自动超时处理，超时后返回初始步骤
- 通过简单的配置文件进行配置
- 支持以守护进程或静默模式运行
- 仅支持 `KEY_ENTER` 和 `KEY_1` 两个按键

## 要求

- 基于 Linux 的系统，能够访问输入设备（例如 `/dev/input/event0`）

## 安装

1. **克隆仓库：**
   ```sh
   git clone https://github.com/gtxaspec/thingino-button.git
   cd thingino-button
   ```

2. **编译程序：**
   要编译程序，可以使用提供的 Makefile。如果需要交叉编译，请设置 `CROSS_COMPILE` 变量。
   ```sh
   make CROSS_COMPILE=mipsel-linux-
   ```

   这将生成 `thingino-button` 可执行文件。

## 使用

1. **创建配置文件：**

   创建一个名为 `/etc/thingino-button.conf` 的文件，并遵循以下格式：

   ```json
   {
     "device": "/dev/input/event0",
     "configuration": [
     "KEY_ENTER":{
         "action": [
            {
               "step_num": 0,
               "prompt": "/usr/bin/play-voice \"检查网络请按KEY_1,下一步请按KEY_ENTER\"",
               "enter": "/usr/bin/play-voice \"进入下一步\"",
               "key1": "/usr/bin/checknet",
               "timeout": 2
            },
            {
               "step_num": 1,
               "prompt": "/usr/bin/play-voice \"这是第二步\"",
               "enter": "/usr/bin/play-voice \"进入第三步\"",
               "key1": "/usr/bin/action2",
               "timeout": 2
            },
            {
               "step_num": 2,
               "prompt": "/usr/bin/play-voice \"这是最后一步\"",
               "enter": "",
               "key1": "/usr/bin/action2",
               "timeout": 2
            }
         ]
      },
      "KEY_1":{
         "action": "/usr/bin/checknet",
         "timeout": 2
      }
     ]
   }
   ```

   配置说明：

   - `device`: 要监控输入事件的设备路径
   - `configuration`: 按键配置数组
     - `KEY_ENTER`: ENTER 键多步骤配置流程
       - `action`: 步骤数组
         - `step_num`: 步骤编号（从 0 开始）
         - `prompt`: 进入此步骤时执行的命令（例如播放语音提示）
         - `enter`: 按下 KEY_ENTER 时执行的命令
         - `key1`: 按下 KEY_1 时执行的命令
         - `timeout`: 超时时间（秒），超时后返回初始步骤
     - `KEY_1`: KEY_1 键快捷命令配置
       - `action`: 初始状态下按 KEY_1 键直接执行的命令
       - `timeout`: 超时时间（秒），超时后返回初始状态

2. **运行程序：**

   您可以直接运行程序或使用可选标志：
   ```sh
   ./thingino-button [-s] [-d] [input_device]
   ```

   - `-s`：以静默模式运行，日志记录到 syslog
   - `-d`：以守护进程模式运行，日志记录到 syslog

   示例：
   ```sh
   ./thingino-button -s /dev/input/event1
   ```

## 按键操作流程说明

### 初始状态
系统处于待机状态，等待用户按键操作。

### 操作分支

**方式一：按 ENTER 键（多步骤配置流程）**
1. 按下 ENTER → 播放第一步语音提示
2. 进入等待状态（可配置超时时间，默认2秒），此时有三种情况：
   - 按 ENTER → 播放第二步语音 → 继续等待（可配置超时时间）
   - 按 KEY_1 → 执行配置的相应命令 → 返回初始状态
   - 超时 → 返回初始状态
3. 第二步后同样进入等待状态，循环上述逻辑
4. 如果没有下一步配置，则返回初始状态

**方式二：初始状态下按 KEY_1 键（快捷命令执行）**
- 在初始状态下按 KEY_1 → 直接执行配置的相应命令 → 返回初始状态

### 流程特点

| 特性 | 说明 |
|------|------|
| 超时时间 | 可配置，默认2秒 |
| ENTER 功能 | 逐步播放语音指引，引导用户完成多步骤配置 |
| KEY_1 功能 | 执行预设命令后返回初始状态 |
| 流程终止条件 | 超时、按 KEY_1 执行命令、或无下一步配置 |

### 简要总结
这是一个**两键交互系统**：初始状态下按 ENTER 键进入**逐步骤语音引导配置**，初始状态下按 KEY_1 键**直接执行相应命令**。在语音引导过程中，用户可以继续按 ENTER 进入下一步，或按 KEY_1 执行命令，每个步骤有可配置的等待超时时间（默认2秒），超时或流程结束后自动返回初始状态。

## 贡献

欢迎贡献！请 fork 仓库并提交拉取请求以进行任何增强或错误修复。

## 许可证

此项目采用 MIT 许可证。
