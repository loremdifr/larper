#ifndef _WIN32
#define _GNU_SOURCE
#endif


#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <signal.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <unistd.h>

//madevice more like smalldevice
#define MA_NO_MP3
#define MA_NO_FLAC
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE

#define MINIAUDIO_IMPLEMENTATION
#include "libs/miniaudio.h"

#include "src/utils.h"
#include "src/keycode_map.h"
#include "src/sounds.h"
#include "src/input_handling.h"
#include "src/version.h"

static int QUIT_SIGNAL = -1;

void setup_signals(){
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	QUIT_SIGNAL = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
}

//

typedef struct {
	const char *program_name;
	const char *command;
	float volume;
	bool randomize_pitch;
	const char *target_folder;
} Arguments;

typedef int (*FnPointer)(void);
typedef struct {
	const char *name;
	const char *short_name;
	FnPointer   action;
	const char *description;
} Command;

static int command_run(void);
static int command_version(void);
static int command_packs(void);
static int command_keys(void);
static int command_help(void);

static Arguments ARGS = {
	.program_name    = "larper",
	.command         = "",
	.volume          = 1.0,
	.randomize_pitch = true,
	.target_folder   = "",
};

static const Command COMMANDS[] = {
	{
		.name        = "run",
		.short_name  = "r",
		.action      = command_run,
		.description = "run larper with a <target> directory",
	},
	{
		.name        = "version",
		.short_name  = "v",
		.action      = command_version,
		.description = "shows the version info",
	},
	{
		.name        = "packs",
		.short_name  = "p",
		.action      = command_packs,
		.description = "information about Sound Packs",
	},
	{
		.name        = "keys",
		.short_name  = "k",
		.action      = command_keys,
		.description = "lists the expected filenames for the WAV files",
	},
	{
		.name        = "help",
		.short_name  = "h",
		.action      = command_help,
		.description = "shows this help message",
	},
};


static void print_error(const char *format, ...){
	const char *RED_COLOR_START = "\033[31m";
	const char *RED_COLOR_END   = "\033[0m";

	fputs("\nERROR: ", stderr);
    fputs(RED_COLOR_START, stderr);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fputs(RED_COLOR_END, stderr);
    fputs("\n\n", stderr);
}

static void print_usage(void){
	printf("USAGE: %s <command> [options] [path]\n\n", ARGS.program_name);
	printf("where [path] is the path to a directory containing ");
	printf("one wav file per key. \nIf not present, it will use the current directory.\n");

	printf("the valid <commands> are:\n");
	for(size_t i = 0; i < ARR_LEN(COMMANDS); i++){
		auto command = COMMANDS[i];
		printf("\t%s\t\t%s\n", command.name, command.description);
	}

	printf("the valid [options] are:\n");
	//TODO: make this dynamic
	printf("\t--volume <value>\tsets the volume to <value>, defaults to 1.0\n");
	printf("\t--no-random-pitch\tdisables per-key random pitch variation\n");

	printf("\n");
}

static int command_version(void){
	printf("larper %s\n", VERSION);
	return EXIT_SUCCESS;
}

static int command_packs(void){
	printf("ABOUT SOUND PACKS:\n");
	printf("1. there's no concept of a `sound pack` in larper.\n");
	printf("2. larper simply uses the WAV files located in the path you point it to.\n");
	printf("3. larper needs no configuration, it uses a convention for the file names.\n");
	printf("   (for a list of expected filenames, use: %s keys)\n", ARGS.program_name);
	printf("\nWHERE TO GET SOUND PACKS?\n");
	printf("1. larper comes with an importer utility to import Sounds Packs from MechVibes.\n");
	printf("2. you can download packs from MechVibes at https://mechvibes.com/sound-packs\n");
	printf("3. once you get the zip file from there, run it:\n");
	printf("   importer <zipfile>\n\n");
	return EXIT_SUCCESS;
}

static int command_keys(void){
	printf(
		"ABOUT KEYS:\n"
		"larper expects to be pointed towards a directory containing "
		"one file per input event (be it a keyboard key press or a mouse button click).\n"
		"if any of the files is missing, that key simply wont play a sound.\n"
		"to make larper as lightweight as possible, it only supports WAV files.\n\n"
		"here's the list of expected filenames, please note these are case sensitive:\n\n"
	);

	for(size_t i = 0; i < ARR_LEN(KEY_NAMES); i++){
		auto key_name = KEY_NAMES[i].name;
		printf(" %s.wav\n", key_name);
	}

	return EXIT_SUCCESS;
}

static int command_help(void){

	printf("larper is a small program that listens to keyboard ");
	printf("and mouse events in the background and plays sounds.\n\n");

	print_usage();
	return EXIT_SUCCESS;
}

static int command_run(void){

	if(strlen(ARGS.target_folder) == 0){
		print_error("missing [path] argument for command 'run'!");
		print_usage();
		return EXIT_FAILURE;
	}

	setup_signals();

	printf("\n> Loading pack: '%s'...", ARGS.target_folder);
	pack_load(ARGS.target_folder);

	if(SOUND_COUNT <= 0){
		printf("Failed!\n");
		print_error(
			"No valid wav files found in directory '%s'",
			ARGS.target_folder
		);
		print_usage();
		return EXIT_FAILURE;
	}

	printf("Ok! (%zu sounds loaded)\n", SOUND_COUNT);

	printf("> %s pitch randomization...", ARGS.randomize_pitch ? "Enabling" : "Disabling");
	ASSERT(set_randomize_pitch(ARGS.randomize_pitch), "set_randomize_pitch failed");
	printf("Ok!\n");

	printf("> Setting volume to '%2.f'...", ARGS.volume);
	ASSERT(set_volume(ARGS.volume), "set_volume failed");
	printf("Ok!\n");

	printf("> Initializing audio...");
	ASSERT(audio_init(), "audio_init failed");
	printf("Ok!\n");

	start_listening_to_keys(QUIT_SIGNAL);

	audio_free();
	close(QUIT_SIGNAL);

	return EXIT_SUCCESS;
}

static void parse_args(int argc, char *argv[argc]){

	//args in the middle are options
	for(int i = 0; i < argc; i++){
		const char *curr_arg = argv[i];

		//program name
		if(i == 0){
			ARGS.program_name = curr_arg;
			continue;
		}

		//command
		if(i == 1){
			ARGS.command = strip_leading_dashes(curr_arg);
			continue;
		}

		//volume
		if(strcmp(curr_arg, "--volume") == 0
		|| strcmp(curr_arg, "-vol")     == 0){
			auto next_arg = argv[++i];
			ARGS.volume = strtof(next_arg, NULL); //convert to float
		}

		//pitch randomization
		if(strcmp(curr_arg, "--no-random-pitch") == 0
		|| strcmp(curr_arg, "-nopitch")          == 0){
			ARGS.randomize_pitch = false;
		}

		//target_folder
		if(i == argc - 1){
			ARGS.target_folder = curr_arg;
			continue;
		}
	}
}

int main(int argc, char *argv[argc]){

	if(argc <= 1){
		print_error("missing required <command> argument.");
		print_usage();
		return EXIT_FAILURE;
	}

	parse_args(argc, argv);

	//see if we can find the command
	for(size_t i = 0; i < ARR_LEN(COMMANDS); i++){
		auto command = COMMANDS[i];
		if(strcmp(command.name, ARGS.command) == 0){
			return command.action();
		}
	}

	print_error("unrecognized command '%s'.", ARGS.command);
	print_usage();
	return EXIT_FAILURE;
}
