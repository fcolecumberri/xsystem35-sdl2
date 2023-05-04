/*
 * sdl_darw.c  SDL event handler
 *
 * Copyright (C) 2000-     Fumihiko Murata       <fmurata@p1.tcnet.ne.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/* $Id: sdl_event.c,v 1.5 2001/12/16 17:12:56 chikama Exp $ */

#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "portab.h"
#include "system.h"
#include "nact.h"
#include "sdl_core.h"
#include "sdl_private.h"
#include "key.h"
#include "menu.h"
#include "input.h"
#include "msgskip.h"
#include "sdl_keytable.h"

static void sdl_getEvent(void);
static void keyEventProsess(SDL_KeyboardEvent *e, boolean pressed);

static uint32_t custom_event_type = (uint32_t)-1;

enum CustomEventCode {
	SIMULATE_RIGHT_BUTTON,
};

/* pointer の状態 */
static int mousex, mousey, mouseb;
static int mouse_wheel_up, mouse_wheel_down;
boolean RawKeyInfo[256];

/* SDL Joystick */
static int joyinfo=0;

void sdl_event_init(void) {
	if (custom_event_type == (uint32_t)-1)
		custom_event_type = SDL_RegisterEvents(1);
}

static int mouse_to_rawkey(int button) {
	switch(button) {
	case SDL_BUTTON_LEFT:
		return KEY_MOUSE_LEFT;
	case SDL_BUTTON_MIDDLE:
		return KEY_MOUSE_MIDDLE;
	case SDL_BUTTON_RIGHT:
		return KEY_MOUSE_RIGHT;
	}
	return 0;
}

static int mouse_to_agsevent(int button) {
	switch(button) {
	case SDL_BUTTON_LEFT:
		return AGSEVENT_BUTTON_LEFT;
	case SDL_BUTTON_MIDDLE:
		return AGSEVENT_BUTTON_MID;
	case SDL_BUTTON_RIGHT:
		return AGSEVENT_BUTTON_RIGHT;
	}
	return 0;
}

EMSCRIPTEN_KEEPALIVE
void send_agsevent(enum agsevent_type type, int code) {
	if (!nact->ags.eventcb)
		return;
	agsevent_t agse = {
		.type = type,
		.code = code,
		.mousex = mousex,
		.mousey = mousey
	};
	nact->ags.eventcb(&agse);  // Async in emscripten
}

// Improves map navigation of Rance4 v2. See also the function comment of
// rance4_Y3_IM_hack() in cmdy.c.
static void rance4v2_hack(void) {
	// Do not wait for vsync while a mouse button or an arrow key is pressed.
	if (mouseb ||
		(RawKeyInfo[KEY_UP]    || RawKeyInfo[KEY_PAD_8]) ||
		(RawKeyInfo[KEY_DOWN]  || RawKeyInfo[KEY_PAD_2]) ||
		(RawKeyInfo[KEY_LEFT]  || RawKeyInfo[KEY_PAD_4]) ||
		(RawKeyInfo[KEY_RIGHT] || RawKeyInfo[KEY_PAD_6]))
		nact->wait_vsync = FALSE;
}

/* Event処理 */
static void sdl_getEvent(void) {
	static int cmd_count_of_prev_input = -1;
	SDL_Event e;
	boolean m2b = FALSE, msg_skip = FALSE;
	boolean had_input = false;

	while (SDL_PollEvent(&e)) {
		had_input = true;

		if (sdl_custom_event_handler && sdl_custom_event_handler(&e))
			continue;

		switch (e.type) {
		case SDL_QUIT:
			menu_quitmenu_open();
			break;

		case SDL_WINDOWEVENT:
			switch (e.window.event) {
			case SDL_WINDOWEVENT_EXPOSED:
				sdl_dirty = TRUE;
				break;
			}
			break;
#ifdef _WIN32
		case SDL_SYSWMEVENT:
			win_menu_onsyswmevent(e.syswm.msg);
			break;
#endif
		case SDL_APP_DIDENTERFOREGROUND:
			sdl_dirty = TRUE;
			break;
		case SDL_KEYDOWN:
			keyEventProsess(&e.key, TRUE);
			break;
		case SDL_KEYUP:
			keyEventProsess(&e.key, FALSE);
			switch (e.key.keysym.sym) {
			case SDLK_F1:
				msg_skip = TRUE;
				break;
			case SDLK_F4:
				sdl_setFullscreen(!sdl_fs_on);
				break;
			}
			break;
		case SDL_MOUSEMOTION:
			mousex = e.motion.x;
			mousey = e.motion.y;
			send_agsevent(AGSEVENT_MOUSE_MOTION, 0);
			break;

		case SDL_MOUSEWHEEL:
			{
				int y = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
				if (y > 0)
					mouse_wheel_up += y;
				else if (y < 0)
					mouse_wheel_down -= y;
				send_agsevent(AGSEVENT_MOUSE_WHEEL, y);
				break;
			}

		case SDL_MOUSEBUTTONDOWN:
			mouseb |= (1 << e.button.button);
			RawKeyInfo[mouse_to_rawkey(e.button.button)] = TRUE;
			send_agsevent(AGSEVENT_BUTTON_PRESS, mouse_to_agsevent(e.button.button));
			break;

		case SDL_MOUSEBUTTONUP:
			mouseb &= (0xffffffff ^ (1 << e.button.button));
			RawKeyInfo[mouse_to_rawkey(e.button.button)] = FALSE;
			send_agsevent(AGSEVENT_BUTTON_RELEASE, mouse_to_agsevent(e.button.button));
			if (e.button.button == 2) {
				m2b = TRUE;
			}
			break;

		case SDL_FINGERDOWN:
			if (SDL_GetNumTouchFingers(e.tfinger.touchId) >= 2) {
				mouseb &= ~(1 << SDL_BUTTON_LEFT);
				mouseb |= 1 << SDL_BUTTON_RIGHT;
				RawKeyInfo[mouse_to_rawkey(SDL_BUTTON_LEFT)] = FALSE;
				RawKeyInfo[mouse_to_rawkey(SDL_BUTTON_RIGHT)] = TRUE;
				send_agsevent(AGSEVENT_BUTTON_PRESS, AGSEVENT_BUTTON_RIGHT);
			} else {
				// SDL_RendererEventWatch clamps touch locations outside of the
				// viewport to 0.0-1.0. Treat such events as right-clicks.
				int button;
				if  (e.tfinger.x == 0.0f || e.tfinger.x == 1.0f || e.tfinger.y == 0.0f || e.tfinger.y == 1.0f) {
					button = SDL_BUTTON_RIGHT;
				} else {
					button = SDL_BUTTON_LEFT;
					mousex = e.tfinger.x * view_w;
					mousey = e.tfinger.y * view_h;
					send_agsevent(AGSEVENT_MOUSE_MOTION, 0);
				}
				mouseb |= 1 << button;
				RawKeyInfo[mouse_to_rawkey(button)] = TRUE;
				send_agsevent(AGSEVENT_BUTTON_PRESS, mouse_to_agsevent(button));
			}
			break;

		case SDL_FINGERUP:
			if (SDL_GetNumTouchFingers(e.tfinger.touchId) == 0) {
				int ags_button = (mouseb & 1 << SDL_BUTTON_LEFT) ? AGSEVENT_BUTTON_LEFT : AGSEVENT_BUTTON_RIGHT;
				mouseb &= ~(1 << SDL_BUTTON_LEFT | 1 << SDL_BUTTON_RIGHT);
				RawKeyInfo[mouse_to_rawkey(SDL_BUTTON_LEFT)] = FALSE;
				RawKeyInfo[mouse_to_rawkey(SDL_BUTTON_RIGHT)] = FALSE;
				mousex = e.tfinger.x * view_w;
				mousey = e.tfinger.y * view_h;
				send_agsevent(AGSEVENT_BUTTON_RELEASE, ags_button);
			}
			break;

		case SDL_FINGERMOTION:
			mousex = e.tfinger.x * view_w;
			mousey = e.tfinger.y * view_h;
			send_agsevent(AGSEVENT_MOUSE_MOTION, 0);
			break;

		case SDL_JOYDEVICEADDED:
			sdl_joy_open(e.jdevice.which);
			break;

		case SDL_JOYAXISMOTION:
			if (abs(e.jaxis.value) < 0x4000) {
				joyinfo &= e.jaxis.axis == 0 ? ~0xc : ~3;
			} else {
				int i = (e.jaxis.axis == 0 ? 2 : 0) +
						(e.jaxis.value > 0 ? 1 : 0);
				joyinfo |= 1 << i;
			}
			break;

		case SDL_JOYBALLMOTION:
			break;

		case SDL_JOYHATMOTION:
			joyinfo &= ~(SYS35KEY_UP | SYS35KEY_DOWN | SYS35KEY_LEFT | SYS35KEY_RIGHT);
			switch (e.jhat.value) {
			case SDL_HAT_UP:        joyinfo |= SYS35KEY_UP;    break;
			case SDL_HAT_DOWN:      joyinfo |= SYS35KEY_DOWN;  break;
			case SDL_HAT_LEFT:      joyinfo |= SYS35KEY_LEFT;  break;
			case SDL_HAT_RIGHT:     joyinfo |= SYS35KEY_RIGHT; break;
			case SDL_HAT_LEFTUP:    joyinfo |= SYS35KEY_LEFT  | SYS35KEY_UP;   break;
			case SDL_HAT_RIGHTUP:   joyinfo |= SYS35KEY_RIGHT | SYS35KEY_UP;   break;
			case SDL_HAT_LEFTDOWN:  joyinfo |= SYS35KEY_LEFT  | SYS35KEY_DOWN; break;
			case SDL_HAT_RIGHTDOWN: joyinfo |= SYS35KEY_RIGHT | SYS35KEY_DOWN; break;
			}
			break;

		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			if (e.jbutton.button < 4) {
				int i = 1 << (e.jbutton.button+4);
				if (e.jbutton.state == SDL_PRESSED)
					joyinfo |= i;
				else
					joyinfo &= ~i;
			} else {
				if (e.jbutton.state == SDL_RELEASED) {
				}
			}
			break;
		default:
			if (e.type == custom_event_type) {
				switch (e.user.code) {
				case SIMULATE_RIGHT_BUTTON:
					if ((intptr_t)e.user.data1) {
						mouseb |= 1 << SDL_BUTTON_RIGHT;
						RawKeyInfo[KEY_MOUSE_RIGHT] = TRUE;
						send_agsevent(AGSEVENT_BUTTON_PRESS, AGSEVENT_BUTTON_RIGHT);
					} else {
						mouseb &= ~(1 << SDL_BUTTON_RIGHT);
						RawKeyInfo[KEY_MOUSE_RIGHT] = FALSE;
						send_agsevent(AGSEVENT_BUTTON_RELEASE, AGSEVENT_BUTTON_RIGHT);
					}
					break;
				}
			}
			break;
		}
	}
	if (had_input) {
		cmd_count_of_prev_input = nact->cmd_count;
	} else if (nact->cmd_count != cmd_count_of_prev_input) {
		nact->wait_vsync = TRUE;
		if (nact->game == GAME_RANCE4_V2)
			rance4v2_hack();
	}
	
	if (m2b) {
		menu_open();
	}
	
	if (msg_skip) msgskip_activate(!msgskip_isActivated());
}

/* キー情報の取得 */
static void keyEventProsess(SDL_KeyboardEvent *e, boolean pressed) {
	int code = sdl_keytable[e->keysym.scancode];
	RawKeyInfo[code] = pressed;
	send_agsevent(pressed ? AGSEVENT_KEY_PRESS : AGSEVENT_KEY_RELEASE, code);
}

int sdl_getKeyInfo() {
	int rt;
	
	sdl_getEvent();
	
	rt = ((RawKeyInfo[KEY_UP]     || RawKeyInfo[KEY_PAD_8])       |
	      ((RawKeyInfo[KEY_DOWN]  || RawKeyInfo[KEY_PAD_2]) << 1) |
	      ((RawKeyInfo[KEY_LEFT]  || RawKeyInfo[KEY_PAD_4]) << 2) |
	      ((RawKeyInfo[KEY_RIGHT] || RawKeyInfo[KEY_PAD_6]) << 3) |
	      (RawKeyInfo[KEY_RETURN] << 4) |
	      (RawKeyInfo[KEY_SPACE ] << 5) |
	      (RawKeyInfo[KEY_ESC]    << 6) |
	      (RawKeyInfo[KEY_TAB]    << 7));
	
	return rt;
}

int sdl_getMouseInfo(MyPoint *p) {
	sdl_getEvent();
	
	if (p) {
		p->x = mousex;
		p->y = mousey;
	}

	int m1 = mouseb & (1 << 1) ? SYS35KEY_RET : 0;
	int m2 = mouseb & (1 << 3) ? SYS35KEY_SPC : 0;
	return m1 | m2;
}

void sdl_getWheelInfo(int *forward, int *back) {
	*forward = mouse_wheel_up;
	*back = mouse_wheel_down;

#ifdef __EMSCRIPTEN__
	EM_ASM( xsystem35.texthook.disableWheelEvent(100) );
#endif
}

void sdl_clearWheelInfo(void) {
	mouse_wheel_up = mouse_wheel_down = 0;
}

int sdl_getJoyInfo(void) {
	sdl_getEvent();
	return joyinfo;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
void simulate_right_button(int pressed) {
	SDL_Event event = {
		.user = {
			.type = custom_event_type,
			.code = SIMULATE_RIGHT_BUTTON,
			.data1 = (void*)pressed
		}
	};
	SDL_PushEvent(&event);
}
#endif
