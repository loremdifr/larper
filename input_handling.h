#ifndef _INPUT_HANDLING_H
#define _INPUT_HANDLING_H

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <assert.h>
#include <stdalign.h>
#include <errno.h>

#include "sounds.h"


constexpr size_t MAX_DEVICES = 64;

typedef struct {
	size_t fd;
	char   path[64];
	char   name[256];
} InputDevice;

typedef struct epoll_event EpollEvent;
typedef struct input_event InputEvent;
typedef struct inotify_event InotifyEvent;

static InputDevice DEVICES[MAX_DEVICES] = {};
static int         EPOLL_FD             = -1;
static int         INOTIFY_FD           = -1;

void start_listening_to_keys(int quit_signal);

static bool _has_event_type(int fd, unsigned long ev_type);
static bool _add_device(const char *path);
static void _remove_device(InputDevice *device);
static int  _scan_devices(void);
static void _handle_event(const InputEvent *input_event);
static void _handle_inotify(void);
static void _handle_device(InputDevice *device, uint32_t events);

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


//this is now called both at init and when a new device is signaled
static bool _add_device(const char *path){

	size_t first_empty_slot = MAX_DEVICES;
	for(size_t i = 0; i < MAX_DEVICES; i++){
		auto device_path = DEVICES[i].path;
		auto is_freed    = (DEVICES[i].fd < 0);

		//device already added
		if(strcmp(device_path, path) != 0 && !is_freed){
			return false;
		}

		if(is_freed && first_empty_slot == MAX_DEVICES){
			first_empty_slot = i;
		}
	}

	//no more empty slots
	if(first_empty_slot == MAX_DEVICES) return false;

	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	ASSERT(fd >= 0, "failed to open device '%s'", path);

	// make sure it's a kb or mouse
	if(!_has_event_type(fd, EV_KEY)
	&& !_has_event_type(fd, EV_REL)){
		close(fd);
		return false;
	}

	InputDevice *curr_device = &DEVICES[first_empty_slot];
	curr_device->fd = fd;
	snprintf(curr_device->path, sizeof(curr_device->path), "%s", path);
	curr_device->name[0] = '\0'; // TODO: not sure if needed
	ioctl(
		fd,
		EVIOCGNAME(sizeof(curr_device->name) - 1),
		curr_device->name
	);

	EpollEvent epoll_ev = {
		.events  = EPOLLIN,
		.data.fd = first_empty_slot
	};

	//actually register fd so that we can watch for events
	if(epoll_ctl(EPOLL_FD, EPOLL_CTL_ADD, fd, &epoll_ev) < 0){
		close(fd);
		return false;
	}

	printf(
		"added device: %s (%s)\n",
		curr_device->name,
		curr_device->path
	);
	return true;
}

static void _remove_device(InputDevice *device){
	epoll_ctl(EPOLL_FD, EPOLL_CTL_DEL, device->fd, NULL);
	close(device->fd);
	device->fd = -1;
	fprintf(stderr, "[!] DEVICE REMOVED: %s\n", device->path);
}

static int _scan_devices(void){
	int devices_count = 0;
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

			printf("\n> trying to register device: %s ...", path);
			if(_add_device(path)){
				printf("Ok!");
				devices_count++;
			}else{
				printf("FAILED");
			}
		}
		entry = readdir(dir);
	}
	closedir(dir);
	return devices_count;
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


static void _handle_inotify(void){
	alignas(InotifyEvent) unsigned char buffer[4096];

	ssize_t bytes_read = read(INOTIFY_FD, buffer, sizeof buffer);
	for(
		char unsigned *offset = buffer;
		bytes_read > 0 && offset < buffer + bytes_read;
	){
		InotifyEvent *inotify_event = (InotifyEvent *)offset;

		if(inotify_event->len
		&& strncmp(inotify_event->name, "event", 5) == 0){
			char path[128];
			snprintf(
				path, sizeof path, "dev/input/%s",
				inotify_event->name
			);
		}

		//advance cursor
		offset += sizeof *inotify_event + inotify_event->len;
	}
}

static void _handle_device(InputDevice *device, uint32_t events){
	//check for fatal error first
	if(events & (EPOLLERR | EPOLLHUP)){
		_remove_device(device);
		return;
	}

	//list pending events for device
	InputEvent input_events[64];
	ssize_t bytes_read = read(device->fd, input_events, sizeof input_events);
	if(bytes_read < 0){
		if(errno != EAGAIN && errno != EINTR){
			//any other type of error means device broke
			_remove_device(device);
			return;
		}
		return; //nothing to process
	}

	//0 means device closed!
	if(bytes_read == 0){
		_remove_device(device);
		return;
	}

	//handle all events for this device
	for(
		size_t i = 0;
		i < (size_t) bytes_read / sizeof input_events[0];
		i++
	){
		_handle_event(&input_events[i]);
	}
}


void start_listening_to_keys(int quit_signal){

	//init all devices to make them usable by _add_device
	for(size_t i = 0; i < MAX_DEVICES; i++){
		DEVICES[i].fd = -1;
	}

	EPOLL_FD = epoll_create1(EPOLL_CLOEXEC);
	ASSERT(EPOLL_FD >= 0, "failed when creating EPOLL_FD");

	INOTIFY_FD = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	ASSERT(EPOLL_FD >= 0, "failed when creating INOTIFY_FD");

	//signal for when devices change
	inotify_add_watch(INOTIFY_FD, "/dev/input", IN_CREATE | IN_ATTRIB);

	printf("> Scanning input devices...\n");
	int devices_count = _scan_devices();
	if(devices_count == 0){
		fprintf(stderr, "ERROR: no input devices found (is it running from the input group?)");
		return;
	}
	printf("Ok! (%d devices found)\n", devices_count);

	printf("\nWaiting for key events...\n");

	EpollEvent events[MAX_DEVICES];
	while(true){
		int fd_count = epoll_wait(EPOLL_FD, events, MAX_DEVICES, -1);
		if(fd_count < 0){
			if(errno == EINTR) continue;
			goto cleanup;
		}

		for(int i = 0; i < fd_count; i++){
			auto curr_event = events[i];
			size_t fd = curr_event.data.fd;
			if(fd == (size_t)quit_signal) goto cleanup;
			if(fd == (size_t)INOTIFY_FD){
				 _handle_inotify();
			}else{
				//dispatch to the correct device
				for(size_t k = 0; k < MAX_DEVICES; k++){
					if(DEVICES[k].fd != fd) continue;
					_handle_device(&DEVICES[k], curr_event.events);
					break;
				}
			}
		}
	}

	cleanup:
	for(size_t i = 0; i < MAX_DEVICES; i++){
		close(DEVICES[i].fd);
		close(EPOLL_FD);
	}
}

#endif //_INPUT_HANDLING_H