/*
 * Thingino Button - 按钮事件处理守护进程
 * 
 * 功能说明：
 * 该程序用于监听输入设备（如按钮）的事件，并根据配置文件执行相应的命令。
 * 支持多步骤流程控制，每个步骤可以配置提示命令、回车键命令、按键1命令和超时时间。
 *
 * 优化内容：
 * 1. 预解析配置到内存结构，避免运行时重复解析JSON
 * 2. 使用select实现高效事件等待和超时检测
 * 3. 修复内存泄漏和缓冲区安全问题
 * 4. 统一错误处理，增强代码健壮性
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>
#include <syslog.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/select.h>
#include <json_config.h>

#define CONFIG_FILE "/etc/thingino-button.conf"
#define DEFAULT_DEVICE "/dev/input/event0"
#define EV_KEY 0x01
#define KEY_ENTER 28
#define KEY_1 2

struct input_event {
	struct timeval time;
	uint16_t type;
	uint16_t code;
	int32_t value;
};
#define MAX_STEPS 10
#define MAX_CONFIG_SIZE (1024 * 1024)
#define MAX_COMMAND_LEN 1024
#define MAX_DEVICE_PATH 256

/* 步骤配置结构体 - 预解析后的配置 */
typedef struct {
	int step_number;
	char prompt_command[MAX_COMMAND_LEN];
	char enter_command[MAX_COMMAND_LEN];
	char key1_command[MAX_COMMAND_LEN];
	int timeout_seconds;
} StepConfig;

/* 全局配置结构体 */
typedef struct {
	char input_device[MAX_DEVICE_PATH];
	StepConfig steps[MAX_STEPS];
	int step_count;
	char key1_action[MAX_COMMAND_LEN];
	int key1_timeout;
	int has_key1_config;
} GlobalConfig;

/* 运行时状态 */
typedef struct {
	int current_step;
	struct timeval step_start_time;
	int status;  /* 0=初始，1=执行中 */
} RuntimeState;

/* 全局变量 */
static GlobalConfig g_config = {0};
static RuntimeState g_state = {0};
static int silent_mode = 0;
static int daemon_mode = 0;
static volatile int running = 1;

/*
 * 日志输出函数
 */
void log_message(const char *format, ...) {
	va_list args;
	va_start(args, format);
	if (silent_mode || daemon_mode) {
		char buf[512];
		vsnprintf(buf, sizeof(buf), format, args);
		syslog(LOG_NOTICE, "%s", buf);
	} else {
		vprintf(format, args);
	}
	va_end(args);
}

/*
 * 安全字符串复制
 */
static int safe_strcpy(char *dest, size_t dest_size, const char *src) {
	if (!dest || !src || dest_size == 0) {
		return -1;
	}
	size_t src_len = strlen(src);
	if (src_len >= dest_size) {
		log_message("Warning: string truncated (len=%zu, max=%zu)\n", src_len, dest_size - 1);
		src_len = dest_size - 1;
	}
	memcpy(dest, src, src_len);
	dest[src_len] = '\0';
	return 0;
}

/*
 * 执行外部命令（非阻塞）
 */
void execute_command(const char *command) {
	if (!command || command[0] == '\0') {
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		log_message("Failed to fork: %s\n", strerror(errno));
		return;
	}
	if (pid == 0) {
		/* 子进程 */
		log_message("Executing: [%s]\n", command);
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		log_message("execl failed: %s\n", strerror(errno));
		_exit(EXIT_FAILURE);
	}
	/* 父进程直接返回 */
}

/*
 * 解析单个步骤配置
 */
static int parse_step_config(JsonValue *step_json, StepConfig *step, int index) {
	JsonValue *step_num = get_object_item(step_json, "step_num");
	JsonValue *prompt = get_object_item(step_json, "prompt");
	JsonValue *enter = get_object_item(step_json, "enter");
	JsonValue *key1 = get_object_item(step_json, "key1");
	JsonValue *timeout = get_object_item(step_json, "timeout");

	if (!step_num || step_num->type != JSON_NUMBER) {
		log_message("Error: Step %d missing step_num\n", index);
		return -1;
	}
	if (!prompt || prompt->type != JSON_STRING || !prompt->value.string) {
		log_message("Error: Step %d missing prompt\n", index);
		return -1;
	}
	if (!enter || enter->type != JSON_STRING || !enter->value.string) {
		log_message("Error: Step %d missing enter\n", index);
		return -1;
	}
	if (!key1 || key1->type != JSON_STRING || !key1->value.string) {
		log_message("Error: Step %d missing key1\n", index);
		return -1;
	}
	if (!timeout || timeout->type != JSON_NUMBER) {
		log_message("Error: Step %d missing timeout\n", index);
		return -1;
	}

	int timeout_val = (int)timeout->value.number;
	if (timeout_val < 1) {
		log_message("Error: Step %d timeout must be >= 1\n", index);
		return -1;
	}

	step->step_number = (int)step_num->value.number;
	safe_strcpy(step->prompt_command, sizeof(step->prompt_command), prompt->value.string);
	safe_strcpy(step->enter_command, sizeof(step->enter_command), enter->value.string);
	safe_strcpy(step->key1_command, sizeof(step->key1_command), key1->value.string);
	step->timeout_seconds = timeout_val;

	return 0;
}

/*
 * 加载并解析配置文件
 *
 * 说明：JSON 解析改为动态链接 libjct（jct 库）提供的接口，
 * 配置文件读取与解析由 libjct 的 parse_json_file 完成。
 */
void load_button_config(void) {
	/* 解析JSON（libjct 负责读取文件并解析） */
	JsonValue *root = parse_json_file(CONFIG_FILE);
	if (!root) {
		log_message("Failed to open or parse config file: %s\n", CONFIG_FILE);
		exit(EXIT_FAILURE);
	}

	/* 提取device字段 */
	JsonValue *device_json = get_object_item(root, "device");
	if (device_json && device_json->type == JSON_STRING && device_json->value.string) {
		safe_strcpy(g_config.input_device, sizeof(g_config.input_device), device_json->value.string);
	} else {
		log_message("Warning: Using default device: %s\n", DEFAULT_DEVICE);
		safe_strcpy(g_config.input_device, sizeof(g_config.input_device), DEFAULT_DEVICE);
	}

	/* 解析configuration */
	JsonValue *configuration = get_object_item(root, "configuration");
	if (!configuration) {
		log_message("Error: configuration section not found\n");
		free_json_value(root);
		exit(EXIT_FAILURE);
	}

	/* 解析KEY_ENTER的多步骤配置 */
	JsonValue *key_enter = get_object_item(configuration, "KEY_ENTER");
	if (key_enter) {
		JsonValue *actions = get_object_item(key_enter, "action");
		if (actions && actions->type == JSON_ARRAY) {
			int count = get_array_size(actions);
			if (count <= 0) {
				log_message("Error: KEY_ENTER.action array is empty\n");
				free_json_value(root);
				exit(EXIT_FAILURE);
			}

			g_config.step_count = (count > MAX_STEPS) ? MAX_STEPS : count;

			for (int i = 0; i < g_config.step_count; i++) {
				JsonValue *step = get_array_item(actions, i);
				if (!step || parse_step_config(step, &g_config.steps[i], i) != 0) {
					log_message("Error: Failed to parse step %d\n", i);
					free_json_value(root);
					exit(EXIT_FAILURE);
				}
			}
			log_message("Loaded %d steps for KEY_ENTER\n", g_config.step_count);
		}
	}

	/* 解析KEY_1配置 */
	JsonValue *key1_config = get_object_item(configuration, "KEY_1");
	if (key1_config) {
		JsonValue *action = get_object_item(key1_config, "action");
		if (action && action->type == JSON_STRING && action->value.string) {
			safe_strcpy(g_config.key1_action, sizeof(g_config.key1_action), action->value.string);
			g_config.has_key1_config = 1;

			JsonValue *timeout = get_object_item(key1_config, "timeout");
			if (timeout && timeout->type == JSON_NUMBER) {
				g_config.key1_timeout = (int)timeout->value.number;
				if (g_config.key1_timeout < 1) {
					g_config.key1_timeout = 2;
				}
			} else {
				g_config.key1_timeout = 2;
			}
		}
	}

	free_json_value(root);
}

/*
 * 进入指定步骤
 */
void enter_step(int step_num) {
	if (step_num < 0 || step_num >= g_config.step_count) {
		log_message("Invalid step number: %d\n", step_num);
		return;
	}

	g_state.current_step = step_num;
	g_state.status = 1;
	gettimeofday(&g_state.step_start_time, NULL);

	log_message("Entering step %d (timeout=%ds)\n",
		step_num, g_config.steps[step_num].timeout_seconds);

	execute_command(g_config.steps[step_num].prompt_command);
}

/*
 * 重置到初始状态
 */
void reset_to_initial(void) {
	log_message("Resetting to initial state\n");
	g_state.status = 0;
	g_state.current_step = 0;
}

/*
 * 检查是否超时
 */
static int check_timeout(void) {
	if (g_state.status == 0 || g_config.step_count == 0) {
		return 0;
	}

	struct timeval now;
	gettimeofday(&now, NULL);

	long elapsed_sec = now.tv_sec - g_state.step_start_time.tv_sec;
	int timeout = g_config.steps[g_state.current_step].timeout_seconds;

	return elapsed_sec >= timeout;
}

/*
 * 处理回车键事件
 */
static void handle_enter_key(void) {
	if (g_state.status == 0) {
		/* 初始状态，启动多步骤流程 */
		enter_step(0);
		return;
	}

	/* 检查超时 */
	if (check_timeout()) {
		log_message("Step %d timeout, resetting\n", g_state.current_step);
		reset_to_initial();
		return;
	}

	/* 执行当前步骤的enter命令 */
	execute_command(g_config.steps[g_state.current_step].enter_command);
	usleep(500000);

	/* 进入下一步或结束 */
	if (g_state.current_step + 1 < g_config.step_count) {
		enter_step(g_state.current_step + 1);
	} else {
		log_message("Reached final step, resetting\n");
		reset_to_initial();
	}
}

/*
 * 处理KEY_1事件
 */
static void handle_key1(void) {
	if (g_state.status == 0) {
		/* 初始状态，执行快捷命令 */
		if (g_config.has_key1_config) {
			log_message("Executing KEY_1 shortcut\n");
			execute_command(g_config.key1_action);
			/* 使用异步方式，不阻塞主循环 */
		}
		return;
	}

	/* 检查超时 */
	if (check_timeout()) {
		log_message("Step %d timeout, resetting\n", g_state.current_step);
		reset_to_initial();
		return;
	}

	/* 执行当前步骤的key1命令 */
	execute_command(g_config.steps[g_state.current_step].key1_command);

	/* 重置状态 */
	reset_to_initial();
}

/*
 * 读取并处理输入事件
 */
static int process_input_event(int fd) {
	char buf[16];
	int n = read(fd, buf, sizeof(buf));

	if (n == 16) {
		struct input_event ev;
		ev.time.tv_sec = *(int32_t *)(buf);
		ev.time.tv_usec = *(int32_t *)(buf + 4);
		ev.type = *(uint16_t *)(buf + 8);
		ev.code = *(uint16_t *)(buf + 10);
		ev.value = *(int32_t *)(buf + 12);

		if (ev.type != EV_KEY) {
			return 0;
		}

		if (ev.value == 1) {
			log_message("Key press: code %d\n", ev.code);
			switch (ev.code) {
				case KEY_ENTER:
					handle_enter_key();
					break;
				case KEY_1:
					handle_key1();
					break;
				default:
					break;
			}
		}
	} else if (n < 0 && errno != EAGAIN) {
		log_message("Error reading event: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

/*
 * 事件处理主循环 - 使用select实现高效等待
 */
void process_events(int fd) {
	fd_set read_fds;
	struct timeval tv;

	while (running) {
		FD_ZERO(&read_fds);
		FD_SET(fd, &read_fds);

		/* 计算select超时时间 */
		if (g_state.status == 1 && g_config.step_count > 0) {
			struct timeval now;
			gettimeofday(&now, NULL);

			long elapsed = now.tv_sec - g_state.step_start_time.tv_sec;
			int timeout = g_config.steps[g_state.current_step].timeout_seconds;
			long remaining = timeout - elapsed;

			if (remaining <= 0) {
				/* 已经超时 */
				log_message("Step %d timeout detected\n", g_state.current_step);
				reset_to_initial();
				tv.tv_sec = 1;
				tv.tv_usec = 0;
			} else {
				tv.tv_sec = remaining;
				tv.tv_usec = 0;
			}
		} else {
			/* 没有活动步骤，使用较长超时 */
			tv.tv_sec = 1;
			tv.tv_usec = 0;
		}

		int ret = select(fd + 1, &read_fds, NULL, NULL, &tv);

		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			}
			log_message("select error: %s\n", strerror(errno));
			break;
		}

		/* 检查超时（select返回0表示超时） */
		if (ret == 0 && g_state.status == 1) {
			if (check_timeout()) {
				log_message("Step %d timeout, resetting\n", g_state.current_step);
				reset_to_initial();
			}
			continue;
		}

		/* 有输入事件 */
		if (FD_ISSET(fd, &read_fds)) {
			if (process_input_event(fd) < 0) {
				break;
			}
		}
	}
}

/*
 * 信号处理函数
 */
void handle_signal(int signal) {
	switch (signal) {
		case SIGTERM:
		case SIGINT:
			log_message("Received signal %d, exiting...\n", signal);
			running = 0;
			break;
	}
}

/*
 * 守护进程化
 */
void daemonize(void) {
	pid_t pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	if (setsid() < 0) {
		exit(EXIT_FAILURE);
	}

	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	umask(0);

	/* 关闭文件描述符 */
	for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
		close(x);
	}

	openlog("thingino-button", LOG_PID, LOG_DAEMON);
}

/*
 * 主函数
 */
int main(int argc, char *argv[]) {
	int opt;
	while ((opt = getopt(argc, argv, "sd")) != -1) {
		switch (opt) {
			case 's':
				silent_mode = 1;
				openlog("thingino-button", LOG_PID | LOG_CONS, LOG_DAEMON);
				setlogmask(LOG_UPTO(LOG_DEBUG));
				break;
			case 'd':
				daemon_mode = 1;
				daemonize();
				silent_mode = 1;
				openlog("thingino-button", LOG_PID | LOG_CONS, LOG_DAEMON);
				setlogmask(LOG_UPTO(LOG_DEBUG));
				break;
			default:
				log_message("Usage: %s [-s] [-d]\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	log_message("thingino-button started\n");

	/* 加载配置 */
	load_button_config();

	if (!silent_mode && !daemon_mode) {
		log_message("Using input device: %s\n", g_config.input_device);
	}

	/* 注册信号处理 */
	signal(SIGTERM, handle_signal);
	signal(SIGINT, handle_signal);

	/* 打开输入设备 */
	int fd = open(g_config.input_device, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		log_message("Failed to open event device: %s\n", strerror(errno));
		return 1;
	}

	log_message("Input device opened successfully\n");

	/* 进入主循环 */
	process_events(fd);

	/* 清理 */
	close(fd);

	if (silent_mode || daemon_mode) {
		closelog();
	}

	log_message("thingino-button exited\n");
	return 0;
}
