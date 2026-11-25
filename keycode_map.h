#ifndef _KEYCODE_MAP_H
#define _KEYCODE_MAP_H

#include <linux/input-event-codes.h>
#include <string.h>

typedef struct {
	const char *name;
	uint16_t    code;
} KeyName;

enum {
	MWHEEL_UP = KEY_MAX + 1,
	MWHEEL_DOWN,
	KEYMAP_COUNT,
};

static const KeyName KEY_NAMES[] = {

	{"MouseWheelUp",    MWHEEL_UP},
	{"MouseWheelDown",  MWHEEL_DOWN},

	{"MouseLeft",    BTN_LEFT},
	{"MouseRight",   BTN_RIGHT},
	{"MouseMiddle",  BTN_MIDDLE},
	{"MouseSide",    BTN_SIDE},
	{"MouseExtra",   BTN_EXTRA},
	{"MouseForward", BTN_FORWARD},
	{"MouseBack",    BTN_BACK},

	{"KeyA", KEY_A},
	{"KeyB", KEY_B},
	{"KeyC", KEY_C},
	{"KeyD", KEY_D},
	{"KeyE", KEY_E},
	{"KeyF", KEY_F},
	{"KeyG", KEY_G},
	{"KeyH", KEY_H},
	{"KeyI", KEY_I},
	{"KeyJ", KEY_J},
	{"KeyK", KEY_K},
	{"KeyL", KEY_L},
	{"KeyM", KEY_M},
	{"KeyN", KEY_N},
	{"KeyO", KEY_O},
	{"KeyP", KEY_P},
	{"KeyQ", KEY_Q},
	{"KeyR", KEY_R},
	{"KeyS", KEY_S},
	{"KeyT", KEY_T},
	{"KeyU", KEY_U},
	{"KeyV", KEY_V},
	{"KeyW", KEY_W},
	{"KeyX", KEY_X},
	{"KeyY", KEY_Y},
	{"KeyZ", KEY_Z},

	{"Digit0", KEY_0},
	{"Digit1", KEY_1},
	{"Digit2", KEY_2},
	{"Digit3", KEY_3},
	{"Digit4", KEY_4},
	{"Digit5", KEY_5},
	{"Digit6", KEY_6},
	{"Digit7", KEY_7},
	{"Digit8", KEY_8},
	{"Digit9", KEY_9},

	{"F1", KEY_F1},
	{"F2", KEY_F2},
	{"F3", KEY_F3},
	{"F4", KEY_F4},
	{"F5", KEY_F5},
	{"F6", KEY_F6},
	{"F7", KEY_F7},
	{"F8", KEY_F8},
	{"F9", KEY_F9},
	{"F10", KEY_F10},
	{"F11", KEY_F11},
	{"F12", KEY_F12},

	{"Numpad0", KEY_KP0},
	{"Numpad1", KEY_KP1},
	{"Numpad2", KEY_KP2},
	{"Numpad3", KEY_KP3},
	{"Numpad4", KEY_KP4},
	{"Numpad5", KEY_KP5},
	{"Numpad6", KEY_KP6},
	{"Numpad7", KEY_KP7},
	{"Numpad8", KEY_KP8},
	{"Numpad9", KEY_KP9},

	{"NumpadEnter",    KEY_KPENTER},
	{"NumpadAdd",      KEY_KPPLUS},
	{"NumpadSubtract", KEY_KPMINUS},
	{"NumpadMultiply", KEY_KPASTERISK},
	{"NumpadDivide",   KEY_KPSLASH},
	{"NumpadDecimal",  KEY_KPDOT},
	{"NumpadEqual",    KEY_KPEQUAL},

	{"Escape",      KEY_ESC},
	{"Backspace",   KEY_BACKSPACE},
	{"Tab",         KEY_TAB},
	{"Enter",       KEY_ENTER},
	{"Space",       KEY_SPACE},
	{"CapsLock",    KEY_CAPSLOCK},
	{"NumLock",     KEY_NUMLOCK},
	{"ScrollLock",  KEY_SCROLLLOCK},
	{"Pause",       KEY_PAUSE},
	{"PrintScreen", KEY_SYSRQ},
	{"ContextMenu", KEY_COMPOSE},

	{"ShiftLeft",    KEY_LEFTSHIFT},
	{"ShiftRight",   KEY_RIGHTSHIFT},
	{"ControlLeft",  KEY_LEFTCTRL},
	{"ControlRight", KEY_RIGHTCTRL},
	{"AltLeft",      KEY_LEFTALT},
	{"AltRight",     KEY_RIGHTALT},
	{"MetaLeft",     KEY_LEFTMETA},
	{"MetaRight",    KEY_RIGHTMETA},

	{"ArrowUp",    KEY_UP},
	{"ArrowDown",  KEY_DOWN},
	{"ArrowLeft",  KEY_LEFT},
	{"ArrowRight", KEY_RIGHT},
	{"Insert",     KEY_INSERT},
	{"Delete",     KEY_DELETE},
	{"Home",       KEY_HOME},
	{"End",        KEY_END},
	{"PageUp",     KEY_PAGEUP},
	{"PageDown",   KEY_PAGEDOWN},

	{"Minus",         KEY_MINUS},
	{"Equal",         KEY_EQUAL},
	{"BracketLeft",   KEY_LEFTBRACE},
	{"BracketRight",  KEY_RIGHTBRACE},
	{"Semicolon",     KEY_SEMICOLON},
	{"Quote",         KEY_APOSTROPHE},
	{"Backquote",     KEY_GRAVE},
	{"Backslash",     KEY_BACKSLASH},
	{"Comma",         KEY_COMMA},
	{"Period",        KEY_DOT},
	{"Slash",         KEY_SLASH},
	{"IntlBackslash", KEY_102ND},
};

#define ARR_LEN(a) (sizeof(a) / sizeof((a)[0]))

static int keycode_from_name(const char *name, size_t len){
	for(size_t i = 0; i < ARR_LEN(KEY_NAMES); i++){
		auto key_name = KEY_NAMES[i].name;
		auto key_code = KEY_NAMES[i].code;

		if(strlen(key_name) == len
		&& memcmp(key_name, name, len) == 0){
			return key_code;
		}
	}
	return -1;
}

#endif //_KEYCODE_MAP_H