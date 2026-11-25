#ifndef _WIN32
#define _GNU_SOURCE
#endif

#include "utils.h"
#include "keycode_map.h"
#include "sounds.h"
#include "input_handling.h"

int main(void){

	//TODO: load from the thingy instead:
	pack_load("/media/nananana/rissu/dev/larper/importer/converted_keyboard");

	printf("\n loaded pack: %zu sounds\n", SOUND_COUNT);

	printf("\nCurrently larping...\n");

	assert(audio_init() && "audio_init failed");

	start_listening_to_keys();

	audio_free();

	return 0;
}
