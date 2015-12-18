/*
 *      greeter-gdk.c - basic ui of lxdm
 *
 *      Copyright 2009 dgod <dgod.osa@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */


#define XLIB_ILLEGAL_ACCESS

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#ifdef ENABLE_GTK3
#include <gdk/gdkkeysyms-compat.h>
#endif
#include <X11/Xlib.h>

#include <string.h>
#include <poll.h>
#include <grp.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>

#include <sys/wait.h>

#include "greeter-utils.h"

#define MAX_INPUT_CHARS     32
#define MAX_VISIBLE_CHARS   14

static Display *dpy;
static GdkWindow *root, *win;
static PangoLayout *layout;
static char user[MAX_INPUT_CHARS];
static char pass[MAX_INPUT_CHARS];
static int stage;
static GdkRectangle rc;
static GdkColor bg, border, hint, text, msg;
    
static char *message;

GKeyFile *config;
static GIOChannel *greeter_io;

static GMainLoop *greeter_loop;

static int get_text_layout(char *s, int *w, int *h)
{
    pango_layout_set_text(layout, s, -1);
    pango_layout_get_pixel_size(layout, w, h);
    return 0;
}

static void draw_text(cairo_t *cr, double x, double y, char *text, GdkColor *color)
{
    pango_layout_set_text(layout, text, -1);
    cairo_move_to(cr, x, y);
    gdk_cairo_set_source_color(cr, color);
    pango_cairo_show_layout(cr, layout);
}

static void on_ui_expose(void)
{
	cairo_t *cr = gdk_cairo_create(win);
	char *p = (stage == 0) ? user : pass;
	int len = strlen(p);
	GdkColor *color=&text;
	
	if(stage==2)
	{
		return;
	}

	gdk_cairo_set_source_color(cr, &bg);
	cairo_rectangle(cr, rc.x, rc.y, rc.width, rc.height);
	cairo_fill(cr);
	gdk_cairo_set_source_color(cr, &border);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	cairo_rectangle(cr, rc.x, rc.y, rc.width, rc.height);

	if( message )
	{
		color = &msg;
		p = message;
	}
	else if( stage == 0 )
	{
		if( len < MAX_VISIBLE_CHARS )
			p = user;
		else
			p = user + len - MAX_VISIBLE_CHARS;
		if( len == 0 )
		{
			p = "Username";
			color = &hint;
		}
	}
	else if( stage >= 1 )
	{
		char spy[MAX_VISIBLE_CHARS + 1];
		p = spy;
		if( len < MAX_VISIBLE_CHARS )
		{
			memset(spy, '*', len);
			p[len] = 0;
		}
		else
		{
			memset(spy, '*', MAX_VISIBLE_CHARS);
			p[MAX_VISIBLE_CHARS] = 0;
		}
		if( len == 0 )
		{
			p = "Password";
			color = &hint;
		}
	}
	draw_text(cr, rc.x+3, rc.y+3, p, color);
	cairo_destroy(cr);
}

static int ui_do_login(void)
{
    if( stage != 2 )
        return -1;

    if( !strcmp(user, "reboot") )
		printf("reboot\n");
    else if(!strcmp(user, "shutdown"))
        printf("shutdown\n");
    else
    {
		char *temp;
		temp=(char*)g_base64_encode((guchar*)pass,strlen(pass)+1);
			printf("login user=%s pass=%s\n",user, temp);
		g_free(temp);
    }
    return 0;
}

static void on_ui_key(GdkEventKey *event)
{
    char *p;
    int len;
    int key;

    if( stage != 0 && stage != 1 )
        return;
    message = 0;
    key = event->keyval;
    p = (stage == 0) ? user : pass;
    len = strlen(p);
    if( key == GDK_Escape )
    {
        user[0] = 0;
        pass[0] = 0;
        stage = 0;
    }
    else if( key == GDK_BackSpace )
    {
        if( len > 0 )
            p[--len] = 0;
    }
    else if( key == GDK_Return )
    {
        if( stage == 0 && len == 0 )
            return;
        stage++;
        if( stage == 1 )
            if( !strcmp(user, "reboot") || !strcmp(user, "shutdown") )
                stage = 2;
    }
    else if( key >= 0x20 && key <= 0x7e )
        if( len < MAX_INPUT_CHARS - 1 )
        {
            p[len++] = key;
            p[len] = 0;
        }
	if(stage==2)
	{
#ifndef ENABLE_GTK3
		gdk_window_clear(win);
#else
		cairo_t *cr=gdk_cairo_create(win);
		cairo_pattern_t *pattern=gdk_window_get_background_pattern(win);
		cairo_set_source(cr,pattern);
		cairo_paint(cr);
		cairo_destroy(cr);
#endif
	}
	else
	{
    	on_ui_expose();
	}
    if( stage == 2 )
    {
        ui_do_login();
        if( stage != 2 )
            on_ui_expose();
    }
}

void ui_event_cb(GdkEvent *event, gpointer data)
{
	if(stage == 2)
		return;
	if(event->type == GDK_KEY_PRESS)
	{
		on_ui_key((GdkEventKey*)event);
	}
	else if(event->type == GDK_EXPOSE)
	{
		on_ui_expose();
	}
}

static void on_screen_size_changed(GdkScreen *screen,GdkWindow *window)
{
	GdkRectangle dest;
	gdk_window_hide(window);
	ui_get_geometry(window,&dest);
	rc.x = dest.x + (dest.width - rc.width) / 2;
	rc.y = dest.y + (dest.height - rc.height) / 2;
	gdk_window_move_resize(window,dest.x,dest.y,dest.width,dest.height);
	ui_set_bg(window,config);
	gdk_window_show(window);
}

void ui_prepare(void)
{
    cairo_t *cr;
    PangoFontDescription *desc;
    char *p;
    int w, h;

    /* get current display */
    dpy = gdk_x11_get_default_xdisplay();
    root = gdk_get_default_root_window();

    user[0] = pass[0] = 0;
    stage = 0;

    p = g_key_file_get_string(config, "input", "border", 0);
    if( !p )
        p = g_strdup("#CBCAE6");
    gdk_color_parse(p, &border);
    g_free(p);

    p = g_key_file_get_string(config, "input", "bg", 0);
    if( !p )
        p = g_strdup("#ffffff");
    gdk_color_parse(p, &bg);
    g_free(p);

    p = g_key_file_get_string(config, "input", "hint", 0);
    if( !p )
        p = g_strdup("#CBCAE6");
    gdk_color_parse(p, &hint);
    g_free(p);

    p = g_key_file_get_string(config, "input", "text", 0);
    if( !p )
        p = g_strdup("#000000");
    gdk_color_parse(p, &text);
    g_free(p);

    p = g_key_file_get_string(config, "input", "msg", 0);
    if( !p )
        p = g_strdup("#ff0000");
    gdk_color_parse(p, &msg);
    g_free(p);

    /* create the window */
    if( !win )
    {
		GdkScreen *scr;
        GdkWindowAttr attr;
        int mask = 0;
        memset( &attr, 0, sizeof(attr) );
        attr.window_type = GDK_WINDOW_TOPLEVEL;
        attr.event_mask = GDK_EXPOSURE_MASK | GDK_KEY_PRESS_MASK;
        attr.wclass = GDK_INPUT_OUTPUT;
        win = gdk_window_new(root, &attr, mask);
        gdk_window_set_decorations(win,0);
        gdk_window_set_title(win,"lxdm-greter-gdk");
        
        scr=gdk_screen_get_default();
        g_signal_connect(scr, "size-changed", G_CALLBACK(on_screen_size_changed), win);
    }

    /* create the font */
    if( layout )
    {
        g_object_unref(layout);
        layout = NULL;
    }
    cr = gdk_cairo_create(win);
    layout = pango_cairo_create_layout(cr);
    cairo_destroy(cr);
    p = g_key_file_get_string(config, "input", "font", 0);
    if( !p ) p = g_strdup("Sans 14");
    desc = pango_font_description_from_string(p);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    g_free(p);

    /* set window size */
    if( layout )
    {
        char temp[MAX_VISIBLE_CHARS + 1 + 1];
        GdkRectangle dest;
        ui_get_geometry(win,&dest);
        memset( temp, 'A', sizeof(temp) );
        temp[sizeof(temp) - 1] = 0;
        get_text_layout(temp, &w, &h);
        rc.width = w + 6; rc.height = h + 6;
        rc.x = dest.x + (dest.width - rc.width) / 2;
        rc.y = dest.y + (dest.height - rc.height) / 2;
        gdk_window_move_resize(win, dest.x, dest.y, dest.width, dest.height);
    }

    /* connect event */
    gdk_window_set_events(win, GDK_EXPOSURE_MASK | GDK_KEY_PRESS_MASK);

    /* draw the first time */
    ui_set_bg(win,config);
    gdk_window_show(win);
    ui_set_focus(win);
}

static gboolean on_lxdm_command(GIOChannel *source, GIOCondition condition, gpointer data)
{
    GIOStatus ret;
    char *str;

    if( !(G_IO_IN & condition) )
        return FALSE;
    ret = g_io_channel_read_line(source, &str, NULL, NULL, NULL);
    if( ret != G_IO_STATUS_NORMAL )
        return FALSE;

    if( !strncmp(str, "quit", 4) || !strncmp(str, "exit",4))
        g_main_loop_quit(greeter_loop);
    else if( !strncmp(str, "reset", 5) )
    {
		user[0] = pass[0] = 0;
    	stage = 0;
    	on_ui_expose();
    }
    g_free(str);
    return TRUE;
}

int main(void)
{
    greeter_loop = g_main_loop_new(NULL, 0);
    
    config = g_key_file_new();
    g_key_file_load_from_file(config, CONFIG_FILE, G_KEY_FILE_KEEP_COMMENTS, NULL);
    
    greeter_io = g_io_channel_unix_new(0);
    g_io_add_watch(greeter_io, G_IO_IN, on_lxdm_command, NULL);

    setvbuf(stdout, NULL, _IOLBF, 0 );
    
    gdk_init(NULL,NULL);

    ui_prepare();
	ui_add_cursor();
	gdk_event_handler_set(ui_event_cb, 0, 0);
	g_main_loop_run(greeter_loop);
    return 0;
}
