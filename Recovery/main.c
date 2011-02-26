#include <stdio.h>
#include <string.h>
#include <pspsdk.h>
#include <pspdebug.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <psputility.h>

#include "systemctrl.h"
#include "systemctrl_se.h"
#include "vshctrl.h"
#include "utils.h"

PSP_MODULE_INFO("Recovery", 0, 1, 2);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define CTRL_REPEAT_TIME 0x40000
#define CUR_SEL_COLOR 0xFF
#define MAX_SCREEN_X 68
#define MAX_SCREEN_Y 33
#define BUF_WIDTH (512)
#define SCR_WIDTH (480)
#define SCR_HEIGHT (272)
#define PIXEL_SIZE (4) /* change this if you change to another screenmode */
#define FRAME_SIZE (BUF_WIDTH * SCR_HEIGHT * PIXEL_SIZE)
#define ZBUF_SIZE (BUF_WIDTH SCR_HEIGHT * 2) /* zbuffer seems to be 16-bit? */

#define printf pspDebugScreenPrintf

static unsigned int __attribute__((aligned(16))) g_disp_list[1024];
static void *buffer = NULL;

static int g_ctrl_OK;
static int g_ctrl_CANCEL;
static u32 g_last_btn = 0;
static u32 g_last_tick = 0;

static char g_bottom_info[MAX_SCREEN_X+1];
static int g_bottom_info_color;
static int g_test_option = 0;

enum {
	TYPE_NORMAL = 0,
	TYPE_SUBMENU = 1,
};

struct MenuEntry {
	char *info;
	int type;
	int color;
	int (*display_callback)(char *, int);
	int (*change_value_callback)(void*, int);
	int (*enter_callback)(void*);
	void *arg;
};

struct Menu {
	char *banner;
	struct MenuEntry *submenu;
	int submenu_size;
	int cur_sel;
	int color;
};

static int sub_menu(void *);

void set_bottom_info(const char *str, int color);
void frame_end(void);

int menu_change_bool(void *arg)
{
	u32 *p = (u32*)arg;
	char buf[256];

	*p = !*p;

	sprintf(buf, "%s: %d\n", __func__, *p);
	set_bottom_info(buf, 0);
	frame_end();
	sceKernelDelayThread(1000000);
	set_bottom_info("", 0);
	
	return 0;
}

struct ChangeInt {
	int value;
	int limit;
};

static struct ChangeInt g_test_option2 = { 
	0,
	4,
};

int display_test_option(char *buf, int size)
{
	return sprintf(buf, "4. test value = %d", g_test_option2.value);
}

int limit_int(int value, int direct, int limit)
{
	if(limit == 0)
		return 0;

	value += direct;

	if(value >= limit) {
		value = value % limit;
	} else if(value < 0) {
		value = limit - ((-value) % limit);
	}

	return value;
}

int menu_change_int(void *arg)
{
	struct ChangeInt *c = arg;
	char buf[256];
	int *p = &(c->value);

	*p = limit_int(*p, 1, c->limit);
	sprintf(buf, "%s: %d\n", __func__, *p);
	set_bottom_info(buf, 0);
	frame_end();
	sceKernelDelayThread(1000000);
	set_bottom_info("", 0);
	
	return 0;
}

int change_test_option(void *arg, int direct)
{
	struct ChangeInt *c = arg;

	c->value = limit_int(c->value, direct, c->limit);

	return 0;
}

struct MenuEntry g_top_menu_entries[] = {
	{ "1. A", 0, },
	{ "2. B", 1, 0xFF00, NULL, NULL, &sub_menu, NULL},
	{ "3. C", 0, 0, NULL, NULL, &menu_change_bool, &g_test_option },
	{ NULL, 0, 0, &display_test_option, &change_test_option, &menu_change_int, &g_test_option2 },
};

struct Menu g_top_menu = {
	"PRO Recovery Menu",
	g_top_menu_entries,
	NELEMS(g_top_menu_entries),
	0,
	0xFF,
};

struct MenuEntry g_sub_menu_entries[] = {
	{ "1. A", 0, },
	{ "2. B", 1, 0xFF00, },
	{ "3. C", 0 },
	{ "4. D", 1, 0xFF00, },
	{ "5. E", 1, 0xFF00, },
};

struct Menu g_sub_menu = {
	"PRO Sub Menu",
	g_sub_menu_entries,
	NELEMS(g_sub_menu_entries),
	0,
	0xFF,
};

void set_screen_xy(int x, int y)
{
	pspDebugScreenSetXY(x, y);
}

static void write_string_with_color(const char *str, int color)
{
	if(color != 0) {
		pspDebugScreenSetTextColor(color);
	}
	
	printf(str);
	pspDebugScreenSetTextColor(0xFFFFFFFF);
}

void write_info_bottom(void)
{
	set_screen_xy(2, MAX_SCREEN_Y-3);
	write_string_with_color(g_bottom_info, g_bottom_info_color);
}

void set_bottom_info(const char *str, int color)
{
	strcpy(g_bottom_info, str);
	g_bottom_info_color = color;
	write_info_bottom();
}

static void draw_button(void)
{
	int i;

	set_screen_xy(0, MAX_SCREEN_Y-5);

	for(i=0; i<MAX_SCREEN_X; ++i) {
		write_string_with_color("*", 0xFF);
	}
}

void draw_menu(struct Menu *menu)
{
	int x, y, i;

	x = 1, y = 1;
	set_screen_xy(x, y);
	write_string_with_color(menu->banner, menu->color);

	x = 3, y = 5;
	set_screen_xy(x, y);

	for(i=0; i<menu->submenu_size; ++i) {
		char buf[256];
		int color;

		if(menu->submenu[i].info != NULL) {
			sprintf(buf, "%s", menu->submenu[i].info);
		} else {
			int (*display_callback)(char *, int);
			
			display_callback = (*menu->submenu[i].display_callback);
			if (display_callback != NULL) {
				(*display_callback)(buf, sizeof(buf));
			} else {
				strcpy(buf, "FIXME");
			}
		}

		if(menu->submenu[i].type == TYPE_SUBMENU) {
			strcat(buf, " ->");
		}

		if(menu->cur_sel == i) {
			color = CUR_SEL_COLOR;
		}
		else {
			color = menu->submenu[i].color;
		}

		write_string_with_color(buf, color);
		set_screen_xy(x, ++y);
	}

	write_info_bottom();
	draw_button();
}

void gu_init()
{
	sceGuInit();
	sceGuStart(GU_DIRECT,g_disp_list);
	sceGuDrawBuffer(GU_PSM_8888,(void*)0,BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH,SCR_HEIGHT,(void*)(512*272*4),BUF_WIDTH);
	sceGuScissor(0,0,SCR_WIDTH,SCR_HEIGHT);
	sceGuEnable(GU_SCISSOR_TEST);
	sceGuFinish();
	sceGuSync(0,0);
	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);
}

u32 ctrl_read(void)
{
	SceCtrlData ctl;

	sceCtrlReadBufferPositive(&ctl, 1);

	if (ctl.Buttons == g_last_btn) {
		if (ctl.TimeStamp - g_last_tick < CTRL_REPEAT_TIME)
			return 0;

		return g_last_btn;
	}

	g_last_btn = ctl.Buttons;
	g_last_tick = ctl.TimeStamp;

	return g_last_btn;
}

void get_confirm_button(void)
{
	int result;

	sceUtilityGetSystemParamInt(9, &result);

	if (result == 0) { // Circle?
		g_ctrl_OK = PSP_CTRL_CIRCLE;
		g_ctrl_CANCEL = PSP_CTRL_CROSS;
	} else {
		g_ctrl_OK = PSP_CTRL_CROSS;
		g_ctrl_CANCEL = PSP_CTRL_CIRCLE;
	}
}

static void get_sel_index(struct Menu *menu, int direct)
{
	menu->cur_sel = limit_int(menu->cur_sel, direct, menu->submenu_size);
}

void frame_end(void)
{
	sceDisplayWaitVblank();
	buffer = sceGuSwapBuffers();
	pspDebugScreenSetOffset((int)buffer);
}

void clear_screen(void)
{
	sceGuStart(GU_DIRECT,g_disp_list);
	sceGuClearColor(0);
	sceGuClear(GU_COLOR_BUFFER_BIT);
	sceGuFinish();
	sceGuSync(0,0);
}

void menu_change_value(struct Menu *menu, int direct)
{
	struct MenuEntry *entry = &menu->submenu[menu->cur_sel];

	if(entry->change_value_callback == NULL)
		return;

	(*entry->change_value_callback)(entry->arg, direct);
}

void menu_loop(struct Menu *menu)
{
	u32 key;

	while (1) {
		clear_screen();
		draw_menu(menu);
		key = ctrl_read();

		switch(key) {
			case PSP_CTRL_UP:
				get_sel_index(menu, -1);
				break;
			case PSP_CTRL_DOWN:
				get_sel_index(menu, +1);
				break;
			case PSP_CTRL_RIGHT:
				menu_change_value(menu, 1);
				break;
			case PSP_CTRL_LEFT:
				menu_change_value(menu, -1);
				break;
		}

		if(key == g_ctrl_OK) {
#ifdef DEBUG
			char info[256];

			sprintf(info, "Enter %s...", menu->submenu[menu->cur_sel].info);
			set_bottom_info(info, 0xFF00);
			frame_end();
			sceKernelDelayThread(1000000);
			set_bottom_info("", 0);
#endif
			if(menu->submenu[menu->cur_sel].enter_callback != NULL) {
				int ret;
				void *arg;

				arg = menu->submenu[menu->cur_sel].arg;
				ret = (*menu->submenu[menu->cur_sel].enter_callback)(arg);
			}
		}

		if(key == g_ctrl_CANCEL) {
			set_bottom_info("Exiting...", 0xFF);
			frame_end();
			sceKernelDelayThread(1000000);
			clear_screen();
			frame_end();
			break;
		}

		frame_end();
	}
}

void main_menu(void)
{
	struct Menu *menu = &g_top_menu;
	
	menu->cur_sel = 0;
	menu_loop(menu);
}

int sub_menu(void * arg)
{
	struct Menu *menu = &g_sub_menu;

	menu->cur_sel = 0;
	menu_loop(menu);

	return 0;
}

int main_thread(SceSize size, void *argp)
{
	gu_init();
	pspDebugScreenInit();
	pspDebugScreenSetOffset((int)buffer);

	get_confirm_button();
	main_menu();

	sceKernelExitDeleteThread(0);

	return 0;
}

int module_start(int argc, char *argv[])
{
	int	thid;
	SceUID thread_id;

	thid = sceKernelCreateThread("recovery_thread", main_thread, 32, 0x8000 ,0 ,0);

	thread_id=thid;

	if (thid>=0) {
		sceKernelStartThread(thid, 0, 0);
	}
	
	return 0;
}

int module_stop(int argc, char *argv[])
{
	return 0;
}
