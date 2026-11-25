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
	ASSERT(fd >= 0, "failed to open device '%s'", path);

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

	// printf(
	// 	"added device: %s (%s)\n",
	// 	curr_device->name,
	// 	curr_device->path
	// );

	DEVICES_COUNT++;
}

static void _scan_devices(void){
	DIR *dir = opendir("/dev/input");
	ASSERT(dir, "failed to open /dev/input");

	DirEntry *entry = readdir(dir);
	// printf("\nscan devices start..");
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
	if(input_event->code >= KEYMAP_COUNT) return;

	//NOTE: we only really care about pressing!
	//value == 1 pressed
	//value == 0 released
	//value == 2 autorepeat
	if(input_event->value != 1) return;

	int keycode = -1;
	switch(input_event->type){
	case EV_KEY: keycode = input_event->code; break;
	case EV_REL:
		//only mouse wheel for this
		//value is +1 -1 for direction up or down
		if(input_event->code != REL_WHEEL) break;
		if(input_event->value == 0)        break;
		keycode = (input_event->value > 0)
			? MWHEEL_UP
			: MWHEEL_DOWN;
		break;
	default: return; //IMPORTANT! otherwise we get sounds for mouse moves
	};

	if(keycode < 0) return; //TODO: not sure if needed..?

	auto sound = KEYMAP[keycode];
	if(sound > NO_SOUND){
 		//pitch randomization!
 		auto pitch_variation = random_step(0.05);
 		// printf("\n PITCH: %.3f\n", pitch_variation);
		queue_push(sound, pitch_variation);
	}
}

void start_listening_to_keys(void){
	EPOLL_FD = epoll_create1(0);
	ASSERT(EPOLL_FD >= 0, "failed when creating epoll fd");

	printf("> Scanning input devices...\n");
	_scan_devices();
	if(DEVICES_COUNT == 0){
		fprintf(stderr, "ERROR: no input devices found (is it running from the input group?)");
		return;
	}
	printf("Ok! (%zu devices found)\n", DEVICES_COUNT);

	// printf("total devices: %zu\n", DEVICES_COUNT);

	printf("\nWaiting for key events...\n");

	EpollEvent events[MAX_DEVICES];
	while(true){
		int fd_count = epoll_wait(EPOLL_FD, events, DEVICES_COUNT, -1);
		ASSERT(fd_count >= 0, "fd_count invalid");

		for(size_t i = fd_count; i-- > 0;){
			size_t idx = events[i].data.u32;
			if(idx >= DEVICES_COUNT) continue; //TODO: should this even possible?

			InputEvent input_ev;
			ssize_t	bytes = read(DEVICES[idx].fd, &input_ev, sizeof(input_ev));

			ASSERT(bytes >= 0, "no device found"); //TODO: better msg

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