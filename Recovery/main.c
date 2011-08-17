/*
 * This file is part of PRO CFW.

 * PRO CFW is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * PRO CFW is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PRO CFW. If not, see <http://www.gnu.org/licenses/ .
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pspsdk.h>
#include <pspdebug.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <psputility.h>

#include "systemctrl.h"
#include "systemctrl_se.h"
#include "vshctrl.h"
#include "utils.h"
#include "vpl.h"
#include "main.h"

PSP_MODULE_INFO("Recovery", 0, 1, 2);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(0);

int g_ctrl_OK;
int g_ctrl_CANCEL;

static u32 g_last_btn = 0;
static u32 g_last_tick = 0;
static u32 g_deadzone_tick = 0;

static u32 no_vsh = 0;

int g_display_flip;

SEConfig g_config;

u32 ctrl_read(void)
{
	SceCtrlData ctl;

	sceCtrlReadBufferPositive(&ctl, 1);

	if (ctl.Buttons == g_last_btn) {
		if (ctl.TimeStamp - g_deadzone_tick < CTRL_DEADZONE_DELAY) {
			return 0;
		}

		if (ctl.TimeStamp - g_last_tick < CTRL_DELAY) {
			return 0;
		}

		g_last_tick = ctl.TimeStamp;

		return g_last_btn;
	}

	g_last_btn = ctl.Buttons;
	g_deadzone_tick = g_last_tick = ctl.TimeStamp;

	return g_last_btn;
}

void get_confirm_button(void)
{
	int result = 0;

	sceUtilityGetSystemParamInt(9, &result);

	if (result == 0) { // Circle?
		g_ctrl_OK = PSP_CTRL_CIRCLE;
		g_ctrl_CANCEL = PSP_CTRL_CROSS;
	} else {
		g_ctrl_OK = PSP_CTRL_CROSS;
		g_ctrl_CANCEL = PSP_CTRL_CIRCLE;
	}
}

void suspend_thread(const char *thread_name)
{
	int ret;

	ret = get_thread_id(thread_name);

	sceKernelSuspendThread(ret);
}

void resume_thread(const char *thread_name)
{
	int ret;

	ret = get_thread_id(thread_name);

	if(ret < 0)
		return;

	sceKernelResumeThread(ret);
}

void suspend_vsh_thread(void)
{
	suspend_thread("SCE_VSH_GRAPHICS");
	suspend_thread("movie_player");
	suspend_thread("audio_buffer");
	suspend_thread("music_player");
}

void resume_vsh_thread(void)
{
	resume_thread("music_player");
	resume_thread("audio_buffer");
	resume_thread("movie_player");
	resume_thread("SCE_VSH_GRAPHICS");
}

void *get_drawing_buffer(void)
{
	void *buffer;

	if(g_display_flip) {
		buffer = DRAW_BUF;
	} else {
		buffer = DISPLAY_BUF;
	}

	return buffer;
}

void *get_display_buffer(void)
{
	void *buffer;

	if(g_display_flip) {
		buffer = DISPLAY_BUF;
	} else {
		buffer = DRAW_BUF;
	}

	return buffer;
}

void recovery_exit(void)
{
	exit_usb();
	vctrlVSHUpdateConfig(&g_config);
	resume_vsh_thread();
	vpl_finish();

	if(no_vsh) {
		sctrlKernelExitVSH(NULL);
	}

	sceKernelStopUnloadSelfModule(0, NULL, NULL, NULL);
}

int main_thread(SceSize size, void *argp)
{
	int thid;

	thid = get_thread_id("SCE_VSH_GRAPHICS");

	if(thid < 0) {
		no_vsh = 1;
	}
	
	sctrlSEGetConfig(&g_config);
	vpl_init();
	suspend_vsh_thread();
	pspDebugScreenInit();
	get_confirm_button();
	main_menu();
	recovery_exit();

	return 0;
}

int module_start(int argc, char *argv[])
{
	int	thid;

	thid = sceKernelCreateThread("recovery_thread", main_thread, 16, 0x8000 ,0 ,0);

	if (thid>=0) {
		sceKernelStartThread(thid, 0, 0);
	}
	
	return 0;
}

int module_stop(int argc, char *argv[])
{
	return 0;
}
