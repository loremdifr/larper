#ifndef _INPUT_HANDLING_H
#define _INPUT_HANDLING_H

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <assert.h>

#include "sounds.h"


constexpr size_t MAX_DEVICES = 64;

typedef struct {
	size_t fd;
	char   path[64];
	char   name[256];
} Device;

typedef struct epoll_event EpollEvent;
typedef struct input_event InputEvent;

static Device DEVICES[MAX_DEVICES] = {};
static size_t DEVICES_COUNT        = 0;
static size_t EPOLL_FD             = -1;

void start_listening_to_keys(void);

static bool _has_event_type(int fd, unsigned long ev_type);
static void _add_device(const char *path);
static void _scan_devices(void);
static void _handle_event(const InputEvent *input_event);

// impl

static bool _has_event_type(int fd, unsigned long ev_type){
	if(ev_type > EV_MAX) return false;

	unsigned long types[(EV_MAX + 7) / 8] = {};

	auto read_types = EVIOCGBIT(0, sizeof(types));
	if(ioctl(fd, read_types, types) < 0) return false;

	unsigned long type_word = types[ev_type / 8];
	unsigned int  type_bit  = ev_type % 8;
	bool          has_type  = (type_word >> type_bit) & 1;

	return has_type;
}


static void _add_device(const char *path){
	// check if we already added this one
	for(size_t i = 0; i < DEVICES_COUNT; i++){
		auto device_path = DEVICES[i].path;
		if(strcmp(device_path, path) == 0) return;
	}

	if(DEVICES_COUNT >= MAX_DEVICES) return;

	int fd = open(path, O_RDONLY | O_NONBLOCK);
	assert(fd >= 0 && "fuck"); //TODO: better msg

	// make sure it's a kb or mouse
	if(!_has_event_type(fd, EV_KEY)
	&& !_has_event_type(fd, EV_REL)){
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

static void _scan_devices(void){
	DIR *dir = opendir("/dev/input");
	assert(dir && "yowch"); //TODO: better msg

	DirEntry *entry = readdir(dir);
	printf("\nscan devices start..");
	while(entry != NULL){
		if(strncmp(entry->d_name, "event", 5) == 0){
			char path[64];
			snprintf(
				path, sizeof(path),
				"/dev/input/%s", entry->d_name
			);
			_add_device(path);
		}
		entry = readdir(dir);
	}
	closedir(dir);
}



static void _handle_event(const InputEvent *input_event){
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

void start_listening_to_keys(void){
	EPOLL_FD = epoll_create1(0);
	assert(EPOLL_FD >= 0 && "help"); //TODO: better msg

	_scan_devices();

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
				_handle_event(&input_ev);
			}
		}
	}

	for(size_t i = 0; i < DEVICES_COUNT; i++){
		close(DEVICES[i].fd);
		close(EPOLL_FD);
	}
}

#endif //_INPUT_HANDLING_H