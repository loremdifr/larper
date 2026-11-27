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

static constexpr char VERSION[] = "larper v0.1.0\n";

static constexpr char ABOUT[] =
"larper is a small program that listens to keyboard "
"and mouse events in the background and plays sounds.\n";

static constexpr char USAGE[] =
"USAGE: %s <path|command>\n\n"
"where <path> is the path to a directory containing "
"one wav file per key.\n";

static constexpr char COMMANDS[] =
"the commands are:\n"
"  version   shows the version info\n"
"  packs     information about Sound Packs\n"
"  keys      lists the expected filenames for the WAV files\n"
"  help      shows this message\n"
;

static constexpr char PACKS[] =
"ABOUT SOUND PACKS:\n"
"1. there's no concept of a `sound pack` in larper.\n"
"2. larper simply uses the WAV files located in the path you point it to.\n"
"3. larper needs no configuration, it uses a convention for the file names.\n"
"   (for a list of expected filenames, use: %s keys)\n"
"\nWHERE TO GET SOUND PACKS?\n"
"1. larper comes with an importer utility to import Sounds Packs from MechVibes.\n"
"2. you can download packs from MechVibes at https://mechvibes.com/sound-packs\n"
"3. once you get the zip file from there, run it:\n"
"   importer <zipfile>\n\n"
;

void print_expected_filenames(){
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

}

static int QUIT_SIGNAL = -1;

void setup_signals(){
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	QUIT_SIGNAL = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
}

int main(int argc, char *argv[argc]){

	const char *program_name = (argc > 0 && argv[0])
		? argv[0]
		: "larper";

	if(argc < 2){
		fprintf(stderr, "ERROR: missing required argument!\n");
		fprintf(stderr, USAGE, program_name);
		fprintf(stderr, COMMANDS);
		return EXIT_FAILURE;
	}

	const char *first_arg = argv[1];

	if(strcmp(first_arg, "version") == 0
	|| strcmp(first_arg, "--version") == 0
	|| strcmp(first_arg, "-v") == 0){
		printf(VERSION);
		return EXIT_SUCCESS;
	}

	if(strcmp(first_arg, "help") == 0
	|| strcmp(first_arg, "--help") == 0
	|| strcmp(first_arg, "-h") == 0){
		printf(ABOUT);
		printf("\n");
		printf(USAGE, program_name);
		printf(COMMANDS);
		return EXIT_SUCCESS;
	}

	if(strcmp(first_arg, "packs") == 0
	|| strcmp(first_arg, "--packs") == 0
	|| strcmp(first_arg, "sounds") == 0
	|| strcmp(first_arg, "soundpacks") == 0){
		printf(PACKS, program_name);
		return EXIT_SUCCESS;
	}

	if(strcmp(first_arg, "keys") == 0
	|| strcmp(first_arg, "--keys") == 0
	|| strcmp(first_arg, "-k") == 0){
		print_expected_filenames();
		return EXIT_SUCCESS;
	}

	setup_signals();

	printf("\n> Loading pack: '%s'...", first_arg);
	pack_load(first_arg);
	printf("Ok! (%zu sounds loaded)\n", SOUND_COUNT);

	printf("> Initializing audio...");
	ASSERT(audio_init(), "audio_init failed");
	printf("Ok!\n");

	start_listening_to_keys(QUIT_SIGNAL);

	audio_free();
	close(QUIT_SIGNAL);
	return EXIT_SUCCESS;
}
