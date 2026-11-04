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

// sound stuff------

constexpr size_t MAX_POLYPHONY = 64;
constexpr size_t QUEUE_SIZE    = 256;

typedef struct {
	const int16_t *sound;
	uint32_t       pos;
	uint32_t       size;
} PlayingSound;

typedef struct {
	uint16_t         queue[QUEUE_SIZE];
	_Atomic uint32_t head;
	_Atomic uint32_t tail;
} SoundQueue;

static PlayingSound SOUNDS_PLAYING[MAX_POLYPHONY] = {};
static SoundQueue   SOUND_QUEUE                   = {};

static void start_sound(size_t sound_id){
	printf("%zu", sound_id);
	//TODO:
	// - find sound by ID
	// - check if any playing sound with pos >= len
	// - otherwise, grab oldest sound
	// - set it up
}

static inline int16_t clamp16(int32_t x){
	constexpr int size16 = 32767;
	if(x >  size16) return  size16;
	if(x < -size16) return -size16;
	return x;
}

static void audio_callback(int16_t *out, uint32_t frames){
	auto qs       = QUEUE_SIZE - 1; //255 for 256, etc..
	SoundQueue *q = &SOUND_QUEUE;

	// check if we should be playing any sound right now
	while(q->tail != atomic_load(&(q->head))){
		start_sound(q->queue[q->tail++ & qs]);
	}

	// fill output buffer
	for(uint32_t frame = 0; frame < frames; frame++){

		//stereo
		int32_t left  = 0;
		int32_t right = 0;

		for(size_t sound = 0; sound < MAX_POLYPHONY; sound++){
			PlayingSound *curr_sound = &SOUNDS_PLAYING[sound];

			if(curr_sound->pos >= curr_sound->size){
				continue;
			}

			left  += curr_sound->sound[curr_sound->pos * 2];
			right += curr_sound->sound[curr_sound->pos * 2 + 1];
			curr_sound->pos++;
		}
		out[frame * 2]     = clamp16(left);
		out[frame * 2 + 1] = clamp16(right);
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
	switch(input_event->type){
	case EV_KEY:
		if(input_event->value != 1) break;
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

int main(void){
	printf("\nCurrently larping...\n");
	start_listening_to_keys();

	return 0;
}
