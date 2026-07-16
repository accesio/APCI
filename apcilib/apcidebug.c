#include <ctype.h>
#include <dirent.h>
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
#define MAX_COMMAND_LENGTH 4096
#define MAX_COMMAND_ARGUMENTS 16
#define MAX_LINE_COMMANDS 64
#define MAX_TOTAL_EXECUTIONS 1000000ULL

typedef struct
{
	int fd;
	int bar;
	int last_width;
	char device_path[PATH_MAX];
} debugger_state_t;

enum parsed_command_type
{
	PARSED_READ,
	PARSED_WRITE,
	PARSED_BAR_SHOW,
	PARSED_BAR_SET,
	PARSED_DEVICE,
	PARSED_HELP,
	PARSED_QUIT
};

typedef struct
{
	enum parsed_command_type type;
	int width;
	int offset;
	uint64_t value;
	uint64_t repeat_count;
} parsed_command_t;

enum command_result
{
	COMMAND_ERROR = -1,
	COMMAND_CONTINUE = 0,
	COMMAND_QUIT = 1
};

static void print_usage(const char *program_name);
static void print_command_help(void);

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

static int tokenize(char *line, char **arguments, int capacity)
{
	char *save;
	char *token;
	int count = 0;

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

static char *trim_whitespace(char *text)
{
	char *end;

	while (isspace((unsigned char)*text))
		text++;
	if (*text == '\0')
		return text;

	end = text + strlen(text) - 1;
	while (end > text && isspace((unsigned char)*end))
		end--;
	end[1] = '\0';
	return text;
}

static int parse_repeat_count(const char *text, uint64_t *value)
{
	char *end;
	unsigned long long parsed;

	if (text == NULL || *text == '\0')
		return -1;

	errno = 0;
	parsed = strtoull(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || parsed == 0 || parsed > MAX_TOTAL_EXECUTIONS)
		return -1;

	*value = parsed;
	return 0;
}

static int parse_standalone_repeat(char *segment, uint64_t *repeat_count)
{
	char *text = trim_whitespace(segment);
	char *count_text;

	if (*text == '*')
	{
		count_text = trim_whitespace(text + 1);
		if (parse_repeat_count(count_text, repeat_count) != 0)
		{
			fprintf(stderr, "Invalid decimal repeat count '%s'.\n", count_text);
			return -1;
		}
		return 1;
	}

	if (strncasecmp(text, "repeat", 6) != 0 || !isspace((unsigned char)text[6]))
		return 0;

	count_text = trim_whitespace(text + 6);
	if (parse_repeat_count(count_text, repeat_count) != 0)
	{
		fprintf(stderr, "Invalid decimal repeat count '%s'.\n", count_text);
		return -1;
	}
	return 1;
}

static int parse_command(char *segment, parsed_command_t *command, int *last_width)
{
	char *arguments[MAX_COMMAND_ARGUMENTS];
	char *star = strchr(segment, '*');
	uint64_t offset;
	uint64_t value;
	uint64_t repeat_count = 1;
	int argument_count;
	int write;
	int width;

	if (star != NULL)
	{
		char *count_text;

		if (strchr(star + 1, '*') != NULL)
		{
			fprintf(stderr, "A command may have only one repeat suffix.\n");
			return -1;
		}
		*star = '\0';
		count_text = trim_whitespace(star + 1);
		if (parse_repeat_count(count_text, &repeat_count) != 0)
		{
			fprintf(stderr, "Invalid decimal repeat count '%s'.\n", count_text);
			return -1;
		}
	}

	segment = trim_whitespace(segment);
	argument_count = tokenize(segment, arguments, MAX_COMMAND_ARGUMENTS);
	if (argument_count <= 0)
	{
		fprintf(stderr, "Missing command before repeat suffix.\n");
		return -1;
	}

	if (argument_count >= 2 && strcasecmp(arguments[argument_count - 2], "repeat") == 0)
	{
		if (star != NULL)
		{
			fprintf(stderr, "Specify either '*N' or 'repeat N', not both.\n");
			return -1;
		}
		if (parse_repeat_count(arguments[argument_count - 1], &repeat_count) != 0)
		{
			fprintf(stderr, "Invalid decimal repeat count '%s'.\n", arguments[argument_count - 1]);
			return -1;
		}
		argument_count -= 2;
		if (argument_count == 0)
		{
			fprintf(stderr, "A standalone line repeat must be the final comma-separated item.\n");
			return -1;
		}
	}

	command->repeat_count = repeat_count;
	command->width = 0;
	command->offset = 0;
	command->value = 0;

	if (strcasecmp(arguments[0], "quit") == 0 || strcasecmp(arguments[0], "exit") == 0 || strcasecmp(arguments[0], "q") == 0)
	{
		if (argument_count != 1)
			goto invalid_usage;
		command->type = PARSED_QUIT;
		return 0;
	}
	if (strcasecmp(arguments[0], "help") == 0 || strcmp(arguments[0], "?") == 0)
	{
		if (argument_count != 1)
			goto invalid_usage;
		command->type = PARSED_HELP;
		return 0;
	}
	if (strcasecmp(arguments[0], "device") == 0)
	{
		if (argument_count != 1)
			goto invalid_usage;
		command->type = PARSED_DEVICE;
		return 0;
	}
	if (strcasecmp(arguments[0], "bar") == 0)
	{
		if (argument_count == 1)
		{
			command->type = PARSED_BAR_SHOW;
			return 0;
		}
		if (argument_count != 2 || parse_hex(arguments[1], 5, &value) != 0)
		{
			fprintf(stderr, "Usage: bar BAR, where BAR is 0 through 5 in hex.\n");
			return -1;
		}
		command->type = PARSED_BAR_SET;
		command->value = value;
		return 0;
	}

	if (strcasecmp(arguments[0], "r") == 0)
	{
		write = 0;
		width = *last_width;
		if (width == 0)
		{
			fprintf(stderr, "'r' requires an earlier width-specific read or write command.\n");
			return -1;
		}
	}
	else if (strcasecmp(arguments[0], "w") == 0)
	{
		write = 1;
		width = *last_width;
		if (width == 0)
		{
			fprintf(stderr, "'w' requires an earlier width-specific read or write command.\n");
			return -1;
		}
	}
	else
	{
		width = command_width(arguments[0], &write);
		if (width == 0)
		{
			fprintf(stderr, "Unknown command '%s'. Enter 'help' for the command list.\n", arguments[0]);
			return -1;
		}
		*last_width = width;
	}

	if ((!write && argument_count != 2) || (write && argument_count != 3))
		goto invalid_usage;
	if (parse_hex(arguments[1], INT_MAX, &offset) != 0)
	{
		fprintf(stderr, "Invalid hexadecimal register offset '%s'.\n", arguments[1]);
		return -1;
	}

	command->type = write ? PARSED_WRITE : PARSED_READ;
	command->width = width;
	command->offset = (int)offset;
	if (!write)
		return 0;

	if (parse_hex(arguments[2], width == 8 ? UINT8_MAX : width == 16 ? UINT16_MAX : UINT32_MAX, &value) != 0)
	{
		fprintf(stderr, "Invalid %d-bit hexadecimal data value '%s'.\n", width, arguments[2]);
		return -1;
	}
	command->value = value;
	return 0;

invalid_usage:
	fprintf(stderr, "Invalid usage for '%s'. Enter 'help' for the command list.\n", arguments[0]);
	return -1;
}

static int parse_command_line(debugger_state_t *state, char *line, parsed_command_t *commands, int *command_count, uint64_t *line_repeat_count)
{
	char *segments[MAX_LINE_COMMANDS + 1];
	char *comment = strchr(line, '#');
	char *cursor;
	char *comma;
	uint64_t executions_per_line = 0;
	int last_width = state->last_width;
	int segment_count = 0;
	int standalone_repeat;

	if (comment != NULL)
		*comment = '\0';
	line = trim_whitespace(line);
	if (*line == '\0')
	{
		*command_count = 0;
		*line_repeat_count = 1;
		return 0;
	}

	cursor = line;
	for (;;)
	{
		if (segment_count == MAX_LINE_COMMANDS + 1)
		{
			fprintf(stderr, "Too many comma-separated commands; maximum is %d.\n", MAX_LINE_COMMANDS);
			return -1;
		}

		comma = strchr(cursor, ',');
		if (comma != NULL)
			*comma = '\0';
		segments[segment_count] = trim_whitespace(cursor);
		if (*segments[segment_count] == '\0')
		{
			fprintf(stderr, "Empty command between commas.\n");
			return -1;
		}
		segment_count++;
		if (comma == NULL)
			break;
		cursor = comma + 1;
	}

	*line_repeat_count = 1;
	standalone_repeat = parse_standalone_repeat(segments[segment_count - 1], line_repeat_count);
	if (standalone_repeat < 0)
		return -1;
	if (standalone_repeat > 0)
	{
		segment_count--;
		if (segment_count == 0)
		{
			fprintf(stderr, "A line repeat requires at least one preceding command.\n");
			return -1;
		}
	}
	if (segment_count > MAX_LINE_COMMANDS)
	{
		fprintf(stderr, "Too many comma-separated commands; maximum is %d.\n", MAX_LINE_COMMANDS);
		return -1;
	}

	for (int i = 0; i < segment_count; i++)
	{
		if (parse_command(segments[i], &commands[i], &last_width) != 0)
			return -1;
		if (executions_per_line > MAX_TOTAL_EXECUTIONS - commands[i].repeat_count)
		{
			fprintf(stderr, "Repeat counts exceed the limit of %llu command executions.\n", MAX_TOTAL_EXECUTIONS);
			return -1;
		}
		executions_per_line += commands[i].repeat_count;
	}

	if (*line_repeat_count > MAX_TOTAL_EXECUTIONS / executions_per_line)
	{
		fprintf(stderr, "Repeat counts exceed the limit of %llu command executions.\n", MAX_TOTAL_EXECUTIONS);
		return -1;
	}

	state->last_width = last_width;
	*command_count = segment_count;
	return 0;
}

static int execute_parsed_command(debugger_state_t *state, const parsed_command_t *command)
{
	switch (command->type)
	{
	case PARSED_READ:
		return read_register(state, command->width, command->offset);
	case PARSED_WRITE:
		return write_register(state, command->width, command->offset, command->value);
	case PARSED_BAR_SHOW:
		printf("BAR = %X\n", state->bar);
		return COMMAND_CONTINUE;
	case PARSED_BAR_SET:
		state->bar = (int)command->value;
		printf("BAR = %X\n", state->bar);
		return COMMAND_CONTINUE;
	case PARSED_DEVICE:
		printf("Device: %s\n", state->device_path);
		return COMMAND_CONTINUE;
	case PARSED_HELP:
		print_command_help();
		return COMMAND_CONTINUE;
	case PARSED_QUIT:
		return COMMAND_QUIT;
	}
	return COMMAND_ERROR;
}

static int execute_command_line(debugger_state_t *state, char *line)
{
	parsed_command_t commands[MAX_LINE_COMMANDS];
	uint64_t line_repeat_count;
	int command_count;

	if (parse_command_line(state, line, commands, &command_count, &line_repeat_count) != 0)
		return COMMAND_ERROR;

	for (uint64_t line_iteration = 0; line_iteration < line_repeat_count; line_iteration++)
	{
		for (int command_index = 0; command_index < command_count; command_index++)
		{
			for (uint64_t command_iteration = 0; command_iteration < commands[command_index].repeat_count; command_iteration++)
			{
				int result = execute_parsed_command(state, &commands[command_index]);

				if (result != COMMAND_CONTINUE)
					return result;
			}
		}
	}
	return COMMAND_CONTINUE;
}

static int run_repl(debugger_state_t *state)
{
	char line[MAX_COMMAND_LENGTH];

	printf("Enter 'help' for commands. BARs, offsets, and data are hexadecimal; repeat counts are decimal.\n");
	for (;;)
	{
		int result;

		printf("apci[BAR%X]> ", state->bar);
		fflush(stdout);
		if (fgets(line, sizeof(line), stdin) == NULL)
		{
			putchar('\n');
			return 0;
		}

		result = execute_command_line(state, line);
		if (result == COMMAND_QUIT)
			return 0;
	}
}

static void print_command_help(void)
{
	printf(
		"Commands (BARs, offsets, and data are hexadecimal):\n"
		"  r8 OFFSET                 Read 8 bits\n"
		"  r16 OFFSET                Read 16 bits\n"
		"  r32 OFFSET                Read 32 bits\n"
		"  r OFFSET                  Read using the previous access width\n"
		"  w8 OFFSET DATA            Write 8 bits\n"
		"  w16 OFFSET DATA           Write 16 bits\n"
		"  w32 OFFSET DATA           Write 32 bits\n"
		"  w OFFSET DATA             Write using the previous access width\n"
		"  bar [BAR]                 Show or select BAR 0 through 5\n"
		"  device                    Show the selected device\n"
		"  help                      Show this help\n"
		"  quit                      Exit\n"
		"\n"
		"The read/write commands also accept read8/read16/read32, write8/write16/write32,\n"
		"and the full apci_read8/apci_write8-style function names.\n"
		"\n"
		"Separate commands with commas. Repeat counts are decimal:\n"
		"  r32 68, r 28, r 30              Run three commands\n"
		"  r32 68, r 28 *10, r 30          Repeat only 'r 28' ten times\n"
		"  r32 68, r 28 *10, r 30, *50     Repeat the entire line fifty times\n"
		"\n"
		"'repeat N' may be used instead of '*N'. The total is limited to 1000000 command executions per line.\n");
}

static void print_usage(const char *program_name)
{
	printf(
		"Usage:\n"
		"  %s [-d DEVICE] [-b BAR]\n"
		"  %s [-d DEVICE] [-b BAR] COMMAND-LINE\n"
		"\n"
		"With no command, starts the interactive debugger. Otherwise it executes the command line\n"
		"and exits. BAR defaults to 1. DEVICE may be a /dev/apci path or basename. BARs, register\n"
		"offsets, and data are bare hexadecimal; repeat counts are decimal. Quote a shell command\n"
		"line containing '*N' to prevent pathname expansion.\n"
		"\n",
		program_name, program_name);
	print_command_help();
}

static int join_arguments(char *line, size_t line_size, int argument_count, char **arguments)
{
	size_t used = 0;

	for (int i = 0; i < argument_count; i++)
	{
		int written = snprintf(line + used, line_size - used, "%s%s", i == 0 ? "" : " ", arguments[i]);

		if (written < 0 || (size_t)written >= line_size - used)
		{
			fprintf(stderr, "Command line is too long; maximum is %d characters.\n", MAX_COMMAND_LENGTH - 1);
			return -1;
		}
		used += (size_t)written;
	}
	return 0;
}

int main(int argc, char **argv)
{
	debugger_state_t state = {.fd = -1, .bar = DEFAULT_BAR, .last_width = 0, .device_path = ""};
	const char *requested_device = NULL;
	char command_line[MAX_COMMAND_LENGTH];
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

	if (open_device(&state, requested_device) != 0)
		return 1;

	if (optind == argc)
		result = run_repl(&state);
	else
	{
		if (join_arguments(command_line, sizeof(command_line), argc - optind, &argv[optind]) != 0)
		{
			close(state.fd);
			return 1;
		}
		result = execute_command_line(&state, command_line);
		result = result == COMMAND_ERROR ? 1 : 0;
	}

	close(state.fd);
	return result;
}
