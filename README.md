# Thingino-Button

`thingino-button` is a simple program designed to monitor input events from a specified device and execute corresponding commands based on step-based configuration. It is primarily aimed at embedded systems where specific actions need to be triggered by pressing buttons in a guided workflow.

## Features

- Monitor an input device for key events.
- Step-based workflow system with configurable steps.
- Execute different commands on KEY_ENTER or KEY_1 press.
- Automatic timeout handling with return to initial step.
- Configurable through a simple configuration file.
- Supports running as a daemon or in silent mode.
- Only supports `KEY_ENTER` and `KEY_1` buttons.

## Requirements

- A Linux-based system with access to the input device (e.g., `/dev/input/event0`).

## Installation

1. **Clone the Repository:**
   ```sh
   git clone https://github.com/gtxaspec/thingino-button.git
   cd thingino-button
   ```

2. **Compile the Program:**
   To compile the program, you can use the provided Makefile. If you are cross-compiling, set the `CROSS_COMPILE` variable.
   ```sh
   make CROSS_COMPILE=mipsel-linux-
   ```

   This will generate the `thingino-button` executable.

## Usage

1. **Create a Configuration File:**

   Create a file named `/etc/thingino-button.conf` with the following format:

   ```json
   {
     "device": "/dev/input/event0",
     "configuration": [
     "KEY_ENTER":{
         "action": [
            {
               "step_num": 0,
               "prompt": "/usr/bin/play-voice \"Press KEY_1 to check network, KEY_ENTER for next step\"",
               "enter": "/usr/bin/play-voice \"Going to next step\"",
               "key1": "/usr/bin/checknet",
               "timeout": 2
            },
            {
               "step_num": 1,
               "prompt": "/usr/bin/play-voice \"This is step 2\"",
               "enter": "/usr/bin/play-voice \"Going to step 3\"",
               "key1": "/usr/bin/action2",
               "timeout": 2
            },
            {
               "step_num": 2,
               "prompt": "/usr/bin/play-voice \"This is the final step\"",
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

   Configuration description:

   - `device`: Path to the input device to be monitored
   - `configuration`: Key configuration object
     - `KEY_ENTER`: ENTER key multi-step configuration workflow
       - `action`: Array of steps
         - `step_num`: Step number (starting from 0)
         - `prompt`: Command to execute when entering this step (e.g., play voice prompt)
         - `enter`: Command to execute when KEY_ENTER is pressed
         - `key1`: Command to execute when KEY_1 is pressed
         - `timeout`: Timeout in seconds before returning to initial step
     - `KEY_1`: KEY_1 key quick command configuration
       - `action`: Command to execute directly when KEY_1 is pressed in initial state
       - `timeout`: Timeout in seconds before returning to initial state

2. **Run the Program:**

   You can run the program directly or with optional flags:
   ```sh
   ./thingino-button [-s] [-d] [input_device]
   ```

   - `-s`: Run in silent mode, logging to syslog.
   - `-d`: Run as a daemon, logging to syslog.

   Example:
   ```sh
   ./thingino-button -s /dev/input/event1
   ```

## Button Operation Workflow

### Initial State
System is in standby state, waiting for user key operations.

### Operation Branches

**Method 1: Press ENTER key (Multi-step configuration workflow)**
1. Press ENTER → Play first step voice prompt
2. Enter waiting state (configurable timeout, default 2 seconds), with three possible scenarios:
   - Press ENTER → Play second step voice → Continue waiting (configurable timeout)
   - Press KEY_1 → Execute configured command → Return to initial state
   - Timeout → Return to initial state
3. After the second step, enter waiting state again and repeat the logic
4. If no further steps are configured, return to initial state

**Method 2: Press KEY_1 key in initial state (Quick command execution)**
- Press KEY_1 in initial state → Directly execute configured command → Return to initial state

### Workflow Features

| Feature | Description |
|---------|-------------|
| Timeout duration | Configurable, default 2 seconds |
| ENTER function | Step-by-step voice guidance for multi-step configuration |
| KEY_1 function | Execute preset commands and return to initial state |
| Termination conditions | Timeout, KEY_1 command execution, or no further steps configured |

### Summary
This is a **two-key interaction system**: Press ENTER in initial state to enter **step-by-step voice-guided configuration**, press KEY_1 in initial state to **directly execute the corresponding command**. During voice guidance, users can continue to press ENTER to proceed to the next step, or press KEY_1 to execute a command. Each step has a configurable waiting timeout (default 2 seconds), and automatically returns to the initial state after timeout or when the workflow completes.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request for any enhancements or bug fixes.

## License

This project is licensed under the MIT License.
