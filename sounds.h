#ifndef _SOUNDS_H
#define _SOUNDS_H

#include <stdatomic.h>
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define MINIAUDIO_IMPLEMENTATION
#include "libs/miniaudio.h"

constexpr size_t MAX_POLYPHONY = 64;
constexpr size_t QUEUE_SIZE    = 256;
constexpr size_t CHANNELS      = 2;
constexpr int16_t NO_SOUND     = -1;
constexpr int SAMPLE_RATE      = 48000;
constexpr int MAX_SOUNDS       = 126;
constexpr size_t KEYMAP_COUNT  = KEY_MAX + 1;

typedef struct {
	const int16_t *sound;
	float          pos;
	float          step;
	uint32_t       size;
} PlayingSound;

typedef struct {
	uint16_t id;
	float    step;
} SoundEvent;

typedef struct {
	SoundEvent       queue[QUEUE_SIZE];
	_Atomic uint32_t head;
	_Atomic uint32_t tail;
} SoundEventQueue;

typedef struct{
	int16_t *audio; // uncompressed!
	uint32_t frames;
} Sound;

typedef struct dirent DirEntry;

static Sound           SOUNDS[MAX_SOUNDS]            = {};
static size_t          SOUND_COUNT                   = 0;
static PlayingSound    SOUNDS_PLAYING[MAX_POLYPHONY] = {};
static SoundEventQueue SOUND_QUEUE                   = {};
static ma_device       AUDIO_DEVICE                  = {};
static int16_t         KEYMAP[KEYMAP_COUNT];

bool pack_load(const char *folder_path);
bool audio_init(void);
void audio_free(void);
bool queue_push(uint16_t id, float step);

static bool _sound_load(Sound *out, const char *path);
static void _start_sound(SoundEvent *sound_event);
static bool _continue_playing_sound(PlayingSound *playing_sound, float *left, float *right);
static void _miniaudio_callback(ma_device *device, void *out, const void *in, ma_uint32 frames);
static void _audio_callback(int16_t *out, uint32_t frames);

//impls

bool queue_push(uint16_t id, float step){
	auto head = atomic_load(&SOUND_QUEUE.head);
	auto tail = atomic_load(&SOUND_QUEUE.tail);

	//queue full
	if(head - tail >= QUEUE_SIZE){
		return false;
	}

	auto index = head % QUEUE_SIZE;
	SOUND_QUEUE.queue[index] = (SoundEvent){.id=id, .step=step};

	//set the new head
	atomic_store(&SOUND_QUEUE.head, head + 1);
	return true;
}

bool audio_init(void){
	auto config = ma_device_config_init(ma_device_type_playback);
	config.playback.format    = ma_format_s16;
	config.playback.channels  = CHANNELS;
	config.sampleRate         = SAMPLE_RATE;
	config.dataCallback       = _miniaudio_callback;
	config.performanceProfile = ma_performance_profile_low_latency;
	config.periodSizeInFrames = 256; //TODO: is this correct..?

	auto try_init = ma_device_init(NULL, &config, &AUDIO_DEVICE);
	if(try_init != MA_SUCCESS){
		return false;
	}

	auto try_start = ma_device_start(&AUDIO_DEVICE);
	return try_start == MA_SUCCESS;
}

void audio_free(void){
	ma_device_uninit(&AUDIO_DEVICE);
}

bool pack_load(const char *folder_path){
	DIR *dir = opendir(folder_path);
	assert(dir && "Failed to open folder_path");

	//init the keymap
	for(size_t i = 0; i < KEYMAP_COUNT; i++){
		KEYMAP[i] = NO_SOUND;
	}

	DirEntry *dir_entry;
	while((dir_entry = readdir(dir))){

		auto filename  = dir_entry->d_name;
		auto length    = strlen(filename);
		auto extension = filename + length - 4;
		if(length <= 4 || strcmp(extension, ".wav") != 0){
			continue;
		}

		auto code = keycode_from_name(filename, length - 4);
		if(code < 0){
			fprintf(stderr, "invalid key name: %s\n", filename);
			continue;
		}

		if(SOUND_COUNT >= MAX_SOUNDS) break;

		char sound_path[1024];
		snprintf(sound_path, sizeof sound_path, "%s/%s", folder_path, filename);
		if(!_sound_load(&SOUNDS[SOUND_COUNT], sound_path)){
			fprintf(stderr, "loading failed for: %s\n", sound_path);
			continue;
		}

		KEYMAP[code] = SOUND_COUNT++;
	}
	closedir(dir);
	return SOUND_COUNT > 0;
}

//

static bool _continue_playing_sound(
	PlayingSound *playing_sound,
	float *left,
	float *right
){
	auto curr_pos = (uint32_t)playing_sound->pos;

	//sound done playing
	if(curr_pos >= playing_sound->size){
		return false;
	}

	auto next_pos = (curr_pos + 1 < playing_sound->size)
		? curr_pos + 1
		: curr_pos;
	float weight = playing_sound->pos - (float)curr_pos;

	const int16_t *from = &playing_sound->sound[curr_pos * CHANNELS];
	const int16_t *to   = &playing_sound->sound[next_pos * CHANNELS];

	*left  += lerpf(from[0], to[0], weight);
	*right += lerpf(from[1], to[1], weight);

	playing_sound->pos += playing_sound->step;

	return true;
}

static void _miniaudio_callback(
	ma_device *device,
	void *out,
	const void *in,
	ma_uint32 frames
){
	//idk??
	(void) device;
	(void) in;
	_audio_callback(out, frames);
}


static void _audio_callback(int16_t *out, uint32_t frames){

	// check if we should be playing any sound right now
	auto tail = atomic_load(&SOUND_QUEUE.tail);
	auto head = atomic_load(&SOUND_QUEUE.head);

	while(tail != head){
		auto index = tail % QUEUE_SIZE;
		_start_sound(&SOUND_QUEUE.queue[index]);
		tail++;
	}

	atomic_store(&SOUND_QUEUE.tail, tail);

	// fill output buffer
	for(uint32_t frame = 0; frame < frames; frame++){

		//stereo
		float left  = 0;
		float right = 0;

		for(size_t sound = 0; sound < MAX_POLYPHONY; sound++){
			auto curr_sound = &SOUNDS_PLAYING[sound];
			_continue_playing_sound(curr_sound, &left, &right);
		}

		out[frame * CHANNELS]     = clamp16(left);
		out[frame * CHANNELS + 1] = clamp16(right);
	 	//NOTE: +1 for right because it's interleaved stereo audio
	}
}

static bool _sound_load(Sound *out, const char *path){
	ma_decoder decoder = {};
	ma_uint64  frames  = 0;
	ma_decoder_config config = ma_decoder_config_init(
		ma_format_s16, CHANNELS, SAMPLE_RATE
	);

	auto try_init = ma_decoder_init_file(
		path, &config, &decoder
	);

	if(try_init != MA_SUCCESS){
		return false;
	}

	auto try_frames = ma_decoder_get_length_in_pcm_frames(
		&decoder, &frames
	);
	if(try_frames != MA_SUCCESS || frames == 0){
		ma_decoder_uninit(&decoder);
		return false;
	}

	int16_t *audio = malloc(sizeof *audio * frames * CHANNELS);
	if(!audio){
		ma_decoder_uninit(&decoder);
		return false;
	}

	ma_uint64 frames_got = 0;
	ma_decoder_read_pcm_frames(
		&decoder, audio, frames, &frames_got
	);
	ma_decoder_uninit(&decoder);

	out->audio  = audio;
	out->frames = (uint32_t) frames_got;
	return true;
}

static void _start_sound(SoundEvent *sound_event){
	auto sound_index = sound_event->id;
	auto sound_step  = sound_event->step;

	if(sound_index >= SOUND_COUNT) return;
	// printf("start sound called with: %zu\n", sound_index);

	auto sound = &SOUNDS[sound_index];

	//slot where we play the sound
	PlayingSound *slot = &SOUNDS_PLAYING[0];

	//find the first available slot
	//if no slots are available, we use the oldest
	for(size_t i = 0; i < MAX_POLYPHONY; i++){
		PlayingSound *playing_sound = &SOUNDS_PLAYING[i];

		//available
		if(playing_sound->pos >= playing_sound->size){
			slot = playing_sound;
			break;
		}

		//oldest (or the one that's closer to done)
		if(playing_sound->pos > slot->pos){
			slot = playing_sound;
		}
	}

	//set the slot to the sound
	slot->sound = sound->audio;
	slot->size  = sound->frames;
	slot->pos   = 0;
	slot->step  = sound_step;
}


#endif //_SOUNDS_H