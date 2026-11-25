#ifndef _WIN32
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <assert.h>
#include "keymap.h"

#define MINIAUDIO_IMPLEMENTATION
#include "libs/miniaudio.h"

//TODO: split into files

// sound stuff------


constexpr size_t MAX_POLYPHONY = 64;
constexpr size_t QUEUE_SIZE    = 256;
constexpr size_t CHANNELS      = 2;
constexpr size_t KEYMAP_COUNT  = KEY_MAX + 1;
constexpr int16_t NO_SOUND     = -1;
constexpr int SAMPLE_RATE      = 48000;
constexpr int MAX_SOUNDS       = 126;

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

static PlayingSound    SOUNDS_PLAYING[MAX_POLYPHONY] = {};
static SoundEventQueue SOUND_QUEUE                   = {};

static int16_t KEYMAP[KEYMAP_COUNT];

static inline int16_t clamp16(int32_t x){
	constexpr int size16 = 32767;
	if(x >  size16) return  size16;
	if(x < -size16) return -size16;
	return x;
}

static inline float lerpf(float from, float to, float weight){
	return from + (to - from) * weight;
}

//TODO: forward def everytihng

static void start_sound(SoundEvent *sound_event);
static bool queue_push(uint16_t id, float step);

static float random_step(float variation){
	if(variation <= 0.0f) return 1.0f;
	auto random = (float)rand() / (float)RAND_MAX;
	auto ranged_rand = (random * 2.0f - 1.0f);
	return 1.0f + ranged_rand * variation;
}

static inline bool continue_playing_sound(
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


static void audio_callback(int16_t *out, uint32_t frames){

	// check if we should be playing any sound right now
	auto tail = atomic_load(&SOUND_QUEUE.tail);
	auto head = atomic_load(&SOUND_QUEUE.head);

	while(tail != head){
		auto index = tail % QUEUE_SIZE;
		start_sound(&SOUND_QUEUE.queue[index]);
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
			continue_playing_sound(curr_sound, &left, &right);
		}

		out[frame * CHANNELS]     = clamp16(left);
		out[frame * CHANNELS + 1] = clamp16(right);
	 	//NOTE: +1 for right because it's interleaved stereo audio
	}
}


// hook into keyboard events -----------------

constexpr size_t MAX_DEVICES = 64;

typedef struct {
	size_t fd;
	char   path[64];
	char   name[256];
} Device;

typedef struct epoll_event EpollEvent;
typedef struct input_event InputEvent;


static Device DEVICES[MAX_DEVICES];
static size_t DEVICES_COUNT = 0;
static size_t EPOLL_FD      = -1;


static bool has_event_type(int fd, unsigned long ev_type){
	if(ev_type > EV_MAX) return false;

	unsigned long types[(EV_MAX + 7) / 8] = {};

	auto read_types = EVIOCGBIT(0, sizeof(types));
	if(ioctl(fd, read_types, types) < 0) return false;

	unsigned long type_word = types[ev_type / 8];
	unsigned int  type_bit  = ev_type % 8;
	bool          has_type  = (type_word >> type_bit) & 1;

	return has_type;
}


static void add_device(const char *path){
	// check if we already added this one
	for(size_t i = 0; i < DEVICES_COUNT; i++){
		auto device_path = DEVICES[i].path;
		if(strcmp(device_path, path) == 0) return;
	}

	if(DEVICES_COUNT >= MAX_DEVICES) return;

	int fd = open(path, O_RDONLY | O_NONBLOCK);
	assert(fd >= 0 && "fuck"); //TODO: better msg

	// make sure it's a kb or mouse
	if(!has_event_type(fd, EV_KEY)
	&& !has_event_type(fd, EV_REL)){
		close(fd);
		return;
	}

	Device *curr_device = &DEVICES[DEVICES_COUNT];
	curr_device->fd = fd;
	snprintf(curr_device->path, sizeof(curr_device->path), "%s", path);
	curr_device->name[0] = '\0'; // TODO: not sure if needed
	ioctl(
		fd,
		EVIOCGNAME(sizeof(curr_device->name) - 1),
		curr_device->name
	);

	//TODO: can the structs be defined as types instead?
	EpollEvent epoll_ev = {
		.events   = EPOLLIN,
		.data.u32 = DEVICES_COUNT
	};

	//TODO: what does this mean?
	if(epoll_ctl(EPOLL_FD, EPOLL_CTL_ADD, fd, &epoll_ev) < 0){
		close(fd);
		return;
	}

	printf(
		"added device: %s (%s)\n",
		curr_device->name,
		curr_device->path
	);

	DEVICES_COUNT++;
}

static void scan_devices(void){
	DIR *dir = opendir("/dev/input");
	assert(dir && "yowch"); //TODO: better msg

	struct dirent *entry = readdir(dir);
	printf("\nscan devices start..");
	while(entry != NULL){
		if(strncmp(entry->d_name, "event", 5) == 0){
			char path[64];
			snprintf(
				path, sizeof(path),
				"/dev/input/%s", entry->d_name
			);
			add_device(path);
		}
		entry = readdir(dir);
	}
	closedir(dir);
}



static void handle_event(const InputEvent *input_event){
	//value == 1 pressed
	//value == 0 released
	//value == 2 autorepeat
	if(input_event->value != 1) return;

	switch(input_event->type){
	case EV_KEY:
		switch(input_event->code){
			case BTN_LEFT:   printf("mouse left\n");  break;
			case BTN_RIGHT:  printf("mouse right\n"); break;
			case BTN_MIDDLE: printf("mouse mid\n");   break;
			default: printf("key:%d\n", input_event->code);
		}
		break;
	case EV_REL:
		if(input_event->code != REL_WHEEL) break;
		printf(
			input_event->value > 0
				? "mwheel up\n"
				: "mwheel down\n"
		);
		break;
	};

	if(input_event->code < KEYMAP_COUNT){
		auto sound = KEYMAP[input_event->code];
		if(sound > NO_SOUND){
	 		//pitch randomization!
	 		auto pitch_variation = random_step(0.05);
	 		printf("\n PITCH: %.3f\n", pitch_variation);
			queue_push(sound, pitch_variation);
		}
	}
}

static void start_listening_to_keys(){
	EPOLL_FD = epoll_create1(0);
	assert(EPOLL_FD >= 0 && "help"); //TODO: better msg

	scan_devices();

	if(DEVICES_COUNT == 0){
		fprintf(stderr, "no devices"); //TODO: better msg
		return;
	}

	printf("total devices: %zu\n", DEVICES_COUNT);

	EpollEvent events[MAX_DEVICES];
	while(true){
		int fd_count = epoll_wait(EPOLL_FD, events, DEVICES_COUNT, -1);
		assert(fd_count >= 0 && "it broke"); //TODO: better msg

		for(size_t i = fd_count; i-- > 0;){
			size_t idx = events[i].data.u32;
			if(idx >= DEVICES_COUNT) continue; //TODO: should this even possible?

			InputEvent input_ev;
			ssize_t	bytes = read(DEVICES[idx].fd, &input_ev, sizeof(input_ev));

			assert(bytes >= 0 && "where is my device???"); //TODO: better msg

			if(bytes == (ssize_t) sizeof(input_ev)){
				handle_event(&input_ev);
			}
		}
	}

	for(size_t i = 0; i < DEVICES_COUNT; i++){
		close(DEVICES[i].fd);
		close(EPOLL_FD);
	}
}



/// miniaudio test

typedef struct{
	int16_t *audio; // uncompressed!
	uint32_t frames;
} Sound;

static Sound SOUNDS[MAX_SOUNDS];
static size_t SOUND_COUNT = 0;


static bool sound_load(Sound *out, const char *path){
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

static void start_sound(SoundEvent *sound_event){
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

static bool queue_push(uint16_t id, float step){
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

static void miniaudio_callback(
	ma_device *device,
	void *out,
	const void *in,
	ma_uint32 frames
){
	//idk??
	(void) device;
	(void) in;
	audio_callback(out, frames);
}

static ma_device AUDIO_DEVICE;

static bool audio_init(void){
	auto config = ma_device_config_init(ma_device_type_playback);
	config.playback.format    = ma_format_s16;
	config.playback.channels  = CHANNELS;
	config.sampleRate         = SAMPLE_RATE;
	config.dataCallback       = miniaudio_callback;
	config.performanceProfile = ma_performance_profile_low_latency;
	config.periodSizeInFrames = 256; //TODO: is this correct..?

	auto try_init = ma_device_init(NULL, &config, &AUDIO_DEVICE);
	if(try_init != MA_SUCCESS){
		return false;
	}

	auto try_start = ma_device_start(&AUDIO_DEVICE);
	return try_start == MA_SUCCESS;
}

typedef struct dirent DirEntry;

static bool pack_load(const char *folder_path){
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
		if(!sound_load(&SOUNDS[SOUND_COUNT], sound_path)){
			fprintf(stderr, "loading failed for: %s\n", sound_path);
			continue;
		}

		KEYMAP[code] = SOUND_COUNT++;
	}
	closedir(dir);
	return SOUND_COUNT > 0;
}

int main(void){

	//TODO: load from the thingy instead:
	pack_load("/media/nananana/rissu/dev/larper/importer/converted_keyboard");

	printf("\n loaded pack: %zu sounds\n", SOUND_COUNT);

	printf("\nCurrently larping...\n");


	// sound_load(&SOUNDS[0], "/media/nananana/rissu/dev/larper/Ratatui.mp3");
	// printf("duration: %.2u", SOUNDS[0].frames / SAMPLE_RATE);
	// SOUND_COUNT = 1;

	assert(audio_init() && "audio_init failed");

	// for(int i = 0; i < 5; i++){
	// 	printf("\n pushing...%d", i);
	// 	queue_push(0);
	// 	sleep(1);
	// }

	// printf("\n all pushed!");

	//map keys
	KEYMAP[KEY_SPACE] = 0;

	start_listening_to_keys();

	ma_device_uninit(&AUDIO_DEVICE);

	///but does it work or not???
	// FILE *raw_audio_file = fopen("test.raw", "wb");
	// fwrite(sound.audio, 4, sound.frames, raw_audio_file);
	// fclose(raw_audio_file);

	return 0;
}
