#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "apcilib.h"

#define APCI_DEVICE_DIRECTORY "/dev/apci"
#define APCI_DEVICE_INDEX 1UL
#define DEFAULT_BAR 1
#define MAX_DEVICES 100
#define MAX_COMMAND_ARGUMENTS 16
#define COMMAND_HISTORY_CAPACITY 100
#define MAX_REPEAT_COUNT 1000000UL

typedef struct
{
	char *entries[COMMAND_HISTORY_CAPACITY];
	size_t start;
	size_t count;
	void *readline_library;
	char *(*readline)(const char *prompt);
	void (*add_history)(const char *line);
} command_history_t;

typedef struct
{
	int fd;
	int bar;
	int last_read_width;
	int last_write_width;
	char device_path[PATH_MAX];
	command_history_t history;
} debugger_state_t;

enum command_result
{
	COMMAND_ERROR = -1,
	COMMAND_CONTINUE = 0,
	COMMAND_QUIT = 1
};

static void print_usage(const char *program_name);
static void print_command_help(void);

static int line_has_text(const char *line)
{
	for (; *line != '\0'; line++)
	{
		if (!isspace((unsigned char)*line))
			return 1;
	}
	return 0;
}

static void command_history_init(command_history_t *history)
{
	static const char *library_names[] = {"libreadline.so.9", "libreadline.so.8", "libreadline.so.7", "libreadline.so"};
	void *readline_symbol;
	void *add_history_symbol;

	memset(history, 0, sizeof(*history));
	for (size_t i = 0; i < sizeof(library_names) / sizeof(library_names[0]); i++)
	{
		history->readline_library = dlopen(library_names[i], RTLD_LAZY | RTLD_LOCAL);
		if (history->readline_library != NULL)
			break;
	}
	if (history->readline_library == NULL)
		return;

	readline_symbol = dlsym(history->readline_library, "readline");
	add_history_symbol = dlsym(history->readline_library, "add_history");
	if (readline_symbol == NULL || add_history_symbol == NULL || sizeof(history->readline) != sizeof(readline_symbol) || sizeof(history->add_history) != sizeof(add_history_symbol))
	{
		dlclose(history->readline_library);
		history->readline_library = NULL;
		return;
	}

	memcpy(&history->readline, &readline_symbol, sizeof(history->readline));
	memcpy(&history->add_history, &add_history_symbol, sizeof(history->add_history));
}

static void command_history_destroy(command_history_t *history)
{
	for (size_t i = 0; i < history->count; i++)
		free(history->entries[(history->start + i) % COMMAND_HISTORY_CAPACITY]);
	if (history->readline_library != NULL)
		dlclose(history->readline_library);
}

static void command_history_add(command_history_t *history, const char *line)
{
	char *copy;
	size_t index;

	if (!line_has_text(line))
		return;
	if (history->add_history != NULL)
		history->add_history(line);

	copy = strdup(line);
	if (copy == NULL)
	{
		fprintf(stderr, "Unable to save command history: %s\n", strerror(errno));
		return;
	}

	if (history->count == COMMAND_HISTORY_CAPACITY)
	{
		index = history->start;
		free(history->entries[index]);
		history->start = (history->start + 1) % COMMAND_HISTORY_CAPACITY;
	}
	else
	{
		index = (history->start + history->count) % COMMAND_HISTORY_CAPACITY;
		history->count++;
	}
	history->entries[index] = copy;
}

static void command_history_print(const command_history_t *history)
{
	for (size_t i = 0; i < history->count; i++)
		printf("%5zu  %s\n", i + 1, history->entries[(history->start + i) % COMMAND_HISTORY_CAPACITY]);
}

static char *read_command_line(command_history_t *history, const char *prompt)
{
	char *line = NULL;
	size_t capacity = 0;
	ssize_t length;

	if (history->readline != NULL && isatty(STDIN_FILENO))
		return history->readline(prompt);

	printf("%s", prompt);
	fflush(stdout);
	length = getline(&line, &capacity, stdin);
	if (length < 0)
	{
		free(line);
		return NULL;
	}
	while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
		line[--length] = '\0';
	return line;
}

static int compare_paths(const void *left, const void *right)
{
	const char *const *left_path = left;
	const char *const *right_path = right;

	return strcmp(*left_path, *right_path);
}

static void free_paths(char **paths, int count)
{
	for (int i = 0; i < count; i++)
		free(paths[i]);
}

static int find_devices(char **paths, int capacity)
{
	DIR *directory = opendir(APCI_DEVICE_DIRECTORY);
	struct dirent *entry;
	int count = 0;

	if (directory == NULL)
	{
		fprintf(stderr, "Unable to open %s: %s\n", APCI_DEVICE_DIRECTORY, strerror(errno));
		fprintf(stderr, "Is apci.ko loaded, and do you have permission to access the devices?\n");
		return -1;
	}

	while ((entry = readdir(directory)) != NULL && count < capacity)
	{
		char path[PATH_MAX];
		struct stat status;
		int length = snprintf(path, sizeof(path), "%s/%s", APCI_DEVICE_DIRECTORY, entry->d_name);

		if (length < 0 || (size_t)length >= sizeof(path))
			continue;
		if (stat(path, &status) != 0 || !S_ISCHR(status.st_mode))
			continue;

		paths[count] = strdup(path);
		if (paths[count] == NULL)
		{
			fprintf(stderr, "Unable to remember device path: %s\n", strerror(errno));
			closedir(directory);
			free_paths(paths, count);
			return -1;
		}
		count++;
	}

	closedir(directory);
	qsort(paths, count, sizeof(paths[0]), compare_paths);
	return count;
}

static int copy_device_path(char *destination, size_t destination_size, const char *requested_path)
{
	int length;

	if (strchr(requested_path, '/') != NULL)
		length = snprintf(destination, destination_size, "%s", requested_path);
	else
		length = snprintf(destination, destination_size, "%s/%s", APCI_DEVICE_DIRECTORY, requested_path);

	if (length < 0 || (size_t)length >= destination_size)
	{
		fprintf(stderr, "Device path is too long.\n");
		return -1;
	}
	return 0;
}

static int prompt_for_device(char *selected_path, size_t selected_path_size)
{
	char *paths[MAX_DEVICES] = {0};
	char input[64];
	int count = find_devices(paths, MAX_DEVICES);
	int selection = 0;

	if (count <= 0)
	{
		if (count == 0)
			fprintf(stderr, "No APCI character devices were found in %s.\n", APCI_DEVICE_DIRECTORY);
		return -1;
	}

	if (count > 1)
	{
		printf("Available APCI devices:\n");
		for (int i = 0; i < count; i++)
			printf("  %d: %s\n", i + 1, paths[i]);

		for (;;)
		{
			char *end;
			long value;

			printf("Select a device (1-%d): ", count);
			fflush(stdout);
			if (fgets(input, sizeof(input), stdin) == NULL)
			{
				fprintf(stderr, "No device selected.\n");
				free_paths(paths, count);
				return -1;
			}

			errno = 0;
			value = strtol(input, &end, 10);
			while (*end == ' ' || *end == '\t')
				end++;
			if (errno == 0 && end != input && (*end == '\n' || *end == '\0') && value >= 1 && value <= count)
			{
				selection = (int)value - 1;
				break;
			}
			printf("Invalid selection.\n");
		}
	}

	if (snprintf(selected_path, selected_path_size, "%s", paths[selection]) >= (int)selected_path_size)
	{
		fprintf(stderr, "Device path is too long.\n");
		free_paths(paths, count);
		return -1;
	}

	free_paths(paths, count);
	return 0;
}

static int open_device(debugger_state_t *state, const char *requested_path)
{
	if (requested_path != NULL)
	{
		if (copy_device_path(state->device_path, sizeof(state->device_path), requested_path) != 0)
			return -1;
	}
	else if (prompt_for_device(state->device_path, sizeof(state->device_path)) != 0)
	{
		return -1;
	}

	state->fd = open(state->device_path, O_RDONLY);
	if (state->fd < 0)
	{
		fprintf(stderr, "Unable to open %s: %s\n", state->device_path, strerror(errno));
		return -1;
	}

	printf("Device: %s\n", state->device_path);
	return 0;
}

static int parse_hex(const char *text, uint64_t maximum, uint64_t *value)
{
	char *end;
	unsigned long long parsed;

	if (text == NULL || *text == '\0' || *text == '-' || *text == '+')
		return -1;

	errno = 0;
	parsed = strtoull(text, &end, 16);
	if (errno != 0 || end == text || *end != '\0' || parsed > maximum)
		return -1;

	*value = parsed;
	return 0;
}

static int command_width(const char *command, int *write)
{
	static const char *read8_names[] = {"r8", "read8", "apci_read8"};
	static const char *read16_names[] = {"r16", "read16", "apci_read16"};
	static const char *read32_names[] = {"r32", "read32", "apci_read32"};
	static const char *write8_names[] = {"w8", "write8", "apci_write8"};
	static const char *write16_names[] = {"w16", "write16", "apci_write16"};
	static const char *write32_names[] = {"w32", "write32", "apci_write32"};
	struct
	{
		const char **names;
		size_t count;
		int width;
		int is_write;
	} groups[] = {
		{read8_names, sizeof(read8_names) / sizeof(read8_names[0]), 8, 0},
		{read16_names, sizeof(read16_names) / sizeof(read16_names[0]), 16, 0},
		{read32_names, sizeof(read32_names) / sizeof(read32_names[0]), 32, 0},
		{write8_names, sizeof(write8_names) / sizeof(write8_names[0]), 8, 1},
		{write16_names, sizeof(write16_names) / sizeof(write16_names[0]), 16, 1},
		{write32_names, sizeof(write32_names) / sizeof(write32_names[0]), 32, 1}
	};

	for (size_t group = 0; group < sizeof(groups) / sizeof(groups[0]); group++)
	{
		for (size_t name = 0; name < groups[group].count; name++)
		{
			if (strcasecmp(command, groups[group].names[name]) == 0)
			{
				*write = groups[group].is_write;
				return groups[group].width;
			}
		}
	}

	return 0;
}

static int report_access_error(const char *operation, const debugger_state_t *state, int offset)
{
	int saved_errno = errno;

	if (saved_errno != 0)
		fprintf(stderr, "%s BAR%X:%04X failed: %s\n", operation, state->bar, offset, strerror(saved_errno));
	else
		fprintf(stderr, "%s BAR%X:%04X failed.\n", operation, state->bar, offset);
	return COMMAND_ERROR;
}

static int read_register(const debugger_state_t *state, int width, int offset)
{
	int status;

	errno = 0;
	switch (width)
	{
	case 8:
	{
		__u8 value = 0;
		status = apci_read8(state->fd, APCI_DEVICE_INDEX, state->bar, offset, &value);
		if (status < 0)
			return report_access_error("apci_read8", state, offset);
		printf("BAR%X:%04X = %02X\n", state->bar, offset, (unsigned int)value);
		break;
	}
	case 16:
	{
		__u16 value = 0;
		status = apci_read16(state->fd, APCI_DEVICE_INDEX, state->bar, offset, &value);
		if (status < 0)
			return report_access_error("apci_read16", state, offset);
		printf("BAR%X:%04X = %04X\n", state->bar, offset, (unsigned int)value);
		break;
	}
	case 32:
	{
		__u32 value = 0;
		status = apci_read32(state->fd, APCI_DEVICE_INDEX, state->bar, offset, &value);
		if (status < 0)
			return report_access_error("apci_read32", state, offset);
		printf("BAR%X:%04X = %08X\n", state->bar, offset, (unsigned int)value);
		break;
	}
	default:
		return COMMAND_ERROR;
	}

	return COMMAND_CONTINUE;
}

static int write_register(const debugger_state_t *state, int width, int offset, uint64_t value)
{
	int status;

	errno = 0;
	switch (width)
	{
	case 8:
		status = apci_write8(state->fd, APCI_DEVICE_INDEX, state->bar, offset, (__u8)value);
		if (status < 0)
			return report_access_error("apci_write8", state, offset);
		printf("BAR%X:%04X <- %02X\n", state->bar, offset, (unsigned int)value);
		break;
	case 16:
		status = apci_write16(state->fd, APCI_DEVICE_INDEX, state->bar, offset, (__u16)value);
		if (status < 0)
			return report_access_error("apci_write16", state, offset);
		printf("BAR%X:%04X <- %04X\n", state->bar, offset, (unsigned int)value);
		break;
	case 32:
		status = apci_write32(state->fd, APCI_DEVICE_INDEX, state->bar, offset, (__u32)value);
		if (status < 0)
			return report_access_error("apci_write32", state, offset);
		printf("BAR%X:%04X <- %08X\n", state->bar, offset, (unsigned int)value);
		break;
	default:
		return COMMAND_ERROR;
	}

	return COMMAND_CONTINUE;
}

static int execute_command(debugger_state_t *state, int argument_count, char **arguments)
{
	uint64_t offset;
	uint64_t value;
	int explicit_width = 1;
	int write;
	int width;

	if (argument_count == 0)
		return COMMAND_CONTINUE;

	if (strcasecmp(arguments[0], "quit") == 0 || strcasecmp(arguments[0], "exit") == 0 || strcasecmp(arguments[0], "q") == 0)
		return COMMAND_QUIT;
	if (strcasecmp(arguments[0], "help") == 0 || strcmp(arguments[0], "?") == 0)
	{
		print_command_help();
		return COMMAND_CONTINUE;
	}
	if (strcasecmp(arguments[0], "history") == 0)
	{
		if (argument_count != 1)
		{
			fprintf(stderr, "Usage: history\n");
			return COMMAND_ERROR;
		}
		command_history_print(&state->history);
		return COMMAND_CONTINUE;
	}
	if (strcasecmp(arguments[0], "device") == 0)
	{
		if (argument_count != 1)
		{
			fprintf(stderr, "Usage: device\n");
			return COMMAND_ERROR;
		}
		printf("Device: %s\n", state->device_path);
		return COMMAND_CONTINUE;
	}
	if (strcasecmp(arguments[0], "bar") == 0)
	{
		if (argument_count == 1)
		{
			printf("BAR = %X\n", state->bar);
			return COMMAND_CONTINUE;
		}
		if (argument_count != 2 || parse_hex(arguments[1], 5, &value) != 0)
		{
			fprintf(stderr, "Usage: bar BAR, where BAR is 0 through 5 in hex.\n");
			return COMMAND_ERROR;
		}
		state->bar = (int)value;
		printf("BAR = %X\n", state->bar);
		return COMMAND_CONTINUE;
	}

	if (strcasecmp(arguments[0], "r") == 0)
	{
		write = 0;
		width = state->last_read_width;
		explicit_width = 0;
		if (width == 0)
		{
			fprintf(stderr, "No previous read width. Use r8, r16, or r32 first.\n");
			return COMMAND_ERROR;
		}
	}
	else if (strcasecmp(arguments[0], "w") == 0)
	{
		write = 1;
		width = state->last_write_width;
		explicit_width = 0;
		if (width == 0)
		{
			fprintf(stderr, "No previous write width. Use w8, w16, or w32 first.\n");
			return COMMAND_ERROR;
		}
	}
	else
	{
		width = command_width(arguments[0], &write);
	}
	if (width == 0)
	{
		fprintf(stderr, "Unknown command '%s'. Enter 'help' for the command list.\n", arguments[0]);
		return COMMAND_ERROR;
	}

	if ((!write && argument_count != 2) || (write && argument_count != 3))
	{
		fprintf(stderr, "Usage: %s OFFSET%s\n", arguments[0], write ? " DATA" : "");
		return COMMAND_ERROR;
	}
	if (parse_hex(arguments[1], INT_MAX, &offset) != 0)
	{
		fprintf(stderr, "Invalid hexadecimal register offset '%s'.\n", arguments[1]);
		return COMMAND_ERROR;
	}
	if (offset % (unsigned int)(width / 8) != 0)
	{
		fprintf(stderr, "Offset %X is not aligned for a %d-bit access; it must be a multiple of %X.\n", (unsigned int)offset, width, (unsigned int)(width / 8));
		return COMMAND_ERROR;
	}

	if (!write)
	{
		if (explicit_width)
			state->last_read_width = width;
		return read_register(state, width, (int)offset);
	}

	if (parse_hex(arguments[2], width == 8 ? UINT8_MAX : width == 16 ? UINT16_MAX : UINT32_MAX, &value) != 0)
	{
		fprintf(stderr, "Invalid %d-bit hexadecimal data value '%s'.\n", width, arguments[2]);
		return COMMAND_ERROR;
	}
	if (explicit_width)
		state->last_write_width = width;
	return write_register(state, width, (int)offset, value);
}

static int parse_repeat_count(const char *text, unsigned long *repeat_count)
{
	char *end;
	unsigned long value;

	if (text == NULL || *text == '\0' || *text == '-' || *text == '+')
		return -1;
	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || value == 0 || value > MAX_REPEAT_COUNT)
		return -1;
	*repeat_count = value;
	return 0;
}

static int execute_with_repeat(debugger_state_t *state, int argument_count, char **arguments)
{
	unsigned long repeat_count = 1;
	const char *repeat_text = NULL;
	int command_argument_count = argument_count;

	if (argument_count > 1 && arguments[argument_count - 1][0] == '*' && arguments[argument_count - 1][1] != '\0')
	{
		repeat_text = &arguments[argument_count - 1][1];
		command_argument_count--;
	}
	else if (argument_count > 2 && (strcmp(arguments[argument_count - 2], "*") == 0 || strcasecmp(arguments[argument_count - 2], "repeat") == 0))
	{
		repeat_text = arguments[argument_count - 1];
		command_argument_count -= 2;
	}

	if (repeat_text != NULL && parse_repeat_count(repeat_text, &repeat_count) != 0)
	{
		fprintf(stderr, "Invalid decimal repeat count '%s'; enter 1 through %lu.\n", repeat_text, MAX_REPEAT_COUNT);
		return COMMAND_ERROR;
	}

	for (unsigned long repetition = 0; repetition < repeat_count; repetition++)
	{
		int result = execute_command(state, command_argument_count, arguments);

		if (result != COMMAND_CONTINUE)
			return result;
	}
	return COMMAND_CONTINUE;
}

static int tokenize(char *line, char **arguments, int capacity)
{
	char *comment = strchr(line, '#');
	char *save;
	char *token;
	int count = 0;

	if (comment != NULL)
		*comment = '\0';

	for (token = strtok_r(line, " \t\r\n", &save); token != NULL; token = strtok_r(NULL, " \t\r\n", &save))
	{
		if (count == capacity)
		{
			fprintf(stderr, "Too many command arguments.\n");
			return -1;
		}
		arguments[count++] = token;
	}
	return count;
}

static int run_repl(debugger_state_t *state)
{
	char *arguments[MAX_COMMAND_ARGUMENTS];

	printf("Enter 'help' for commands. Register offsets and data are hexadecimal.\n");
	for (;;)
	{
		char prompt[32];
		char *line;
		int argument_count;
		int result;

		snprintf(prompt, sizeof(prompt), "apci[BAR%X]> ", state->bar);
		line = read_command_line(&state->history, prompt);
		if (line == NULL)
		{
			putchar('\n');
			return 0;
		}

		command_history_add(&state->history, line);
		argument_count = tokenize(line, arguments, MAX_COMMAND_ARGUMENTS);
		if (argument_count < 0)
		{
			free(line);
			continue;
		}
		result = execute_with_repeat(state, argument_count, arguments);
		free(line);
		if (result == COMMAND_QUIT)
			return 0;
	}
}

static void print_command_help(void)
{
	printf(
		"Commands (register offsets and data are hexadecimal):\n"
		"  r8 OFFSET                 Read 8 bits\n"
		"  r16 OFFSET                Read 16 bits\n"
		"  r32 OFFSET                Read 32 bits\n"
		"  r OFFSET                  Read using the last explicit read width\n"
		"  w8 OFFSET DATA            Write 8 bits\n"
		"  w16 OFFSET DATA           Write 16 bits\n"
		"  w32 OFFSET DATA           Write 32 bits\n"
		"  w OFFSET DATA             Write using the last explicit write width\n"
		"  bar [BAR]                 Show or select BAR 0 through 5\n"
		"  device                    Show the selected device\n"
		"  history                   Show command history\n"
		"  help                      Show this help\n"
		"  quit                      Exit\n"
		"\n"
		"Suffix a command with *N to execute it N total times; N is decimal.\n"
		"For example: r32 68 *10. The suffix forms '* N' and 'repeat N' also work.\n"
		"Up/down arrows recall previous interactive commands.\n"
		"\n"
		"The read/write commands also accept read8/read16/read32, write8/write16/write32,\n"
		"and the full apci_read8/apci_write8-style function names.\n");
}

static void print_usage(const char *program_name)
{
	printf(
		"Usage:\n"
		"  %s [-d DEVICE] [-b BAR]\n"
		"  %s [-d DEVICE] [-b BAR] COMMAND [ARGUMENT ...]\n"
		"\n"
		"With no command, starts the interactive debugger. With a command, performs one\n"
		"operation and exits. BAR defaults to 1. DEVICE may be a /dev/apci path or basename.\n"
		"Register offsets and data are hexadecimal; bare values such as 68 and FFFFFFFF are valid.\n"
		"Repeat counts are decimal.\n"
		"\n",
		program_name, program_name);
	print_command_help();
}

int main(int argc, char **argv)
{
	debugger_state_t state = {.fd = -1, .bar = DEFAULT_BAR, .device_path = ""};
	const char *requested_device = NULL;
	uint64_t value;
	int option;
	int result;

	setvbuf(stdout, NULL, _IOLBF, 0);

	while ((option = getopt(argc, argv, "b:d:h")) != -1)
	{
		switch (option)
		{
		case 'b':
			if (parse_hex(optarg, 5, &value) != 0)
			{
				fprintf(stderr, "Invalid BAR '%s'; enter 0 through 5 in hex.\n", optarg);
				return 1;
			}
			state.bar = (int)value;
			break;
		case 'd':
			requested_device = optarg;
			break;
		case 'h':
			print_usage(argv[0]);
			return 0;
		default:
			print_usage(argv[0]);
			return 1;
		}
	}

	command_history_init(&state.history);
	if (open_device(&state, requested_device) != 0)
	{
		command_history_destroy(&state.history);
		return 1;
	}

	if (optind == argc)
		result = run_repl(&state);
	else
	{
		result = execute_with_repeat(&state, argc - optind, &argv[optind]);
		result = result == COMMAND_ERROR ? 1 : 0;
	}

	close(state.fd);
	command_history_destroy(&state.history);
	return result;
}
