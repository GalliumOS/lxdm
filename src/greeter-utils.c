#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <string.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <unistd.h>

static cairo_surface_t *cairo_surface_create_from_pixbuf (GdkWindow *root,GdkPixbuf *pixbuf)
{
	Display *dpy=GDK_WINDOW_XDISPLAY(root);
	int scr=DefaultScreen(dpy);
	Visual *visual=DefaultVisual(dpy,scr);
	gint width=gdk_pixbuf_get_width(pixbuf);
	gint height=gdk_pixbuf_get_height(pixbuf);
	Pixmap pix=XCreatePixmap(dpy,GDK_WINDOW_XID(root),width,height,DefaultDepth(dpy,scr));
	cairo_surface_t *surface=cairo_xlib_surface_create(dpy,pix,visual,width,height);
	cairo_t *cr=cairo_create(surface);
	gdk_cairo_set_source_pixbuf(cr,pixbuf,0,0);
	cairo_paint(cr);
	cairo_destroy(cr);
	return surface;
}

int ui_get_geometry(GdkWindow *win,GdkRectangle *rc)
{
#if GTK_CHECK_VERSION(2,24,0)
	GdkScreen *screen=gdk_window_get_screen(win);
#else
	GdkScreen *screen=gdk_screen_get_default();
#endif
#if GTK_CHECK_VERSION(2,24,0)
	gint monitor=gdk_screen_get_primary_monitor(screen);
#else
	gint monitor=0;
#endif
	gdk_screen_get_monitor_geometry(screen,monitor,rc);
	return 0;
}

void ui_set_bg(GdkWindow *win,GKeyFile *config)
{
	GdkPixbuf *bg_img=NULL;
#if GTK_CHECK_VERSION(3,4,0)
	GdkRGBA bg_color;
#else
	GdkColor bg_color;
#endif
	GdkWindow *root=gdk_get_default_root_window();
	char *p=g_key_file_get_string(config,"display","bg",NULL);
#if GTK_CHECK_VERSION(3,4,0)
	gdk_rgba_parse(&bg_color,"#222E45");
#else
	gdk_color_parse("#222E45",&bg_color);
#endif
	if( p && p[0] != '#' )
	{
		bg_img = gdk_pixbuf_new_from_file(p, 0);
	}
	if( p && p[0] == '#' )
	{
#if GTK_CHECK_VERSION(3,4,0)
		gdk_rgba_parse(&bg_color,p);
#else
		gdk_color_parse(p,&bg_color);
#endif
	}
	g_free(p);

    /* set background */
	if( bg_img )
	{
		p = g_key_file_get_string(config, "display", "bg_style", 0);
		if( !p || !strcmp(p, "stretch") )
		{
			GdkPixbuf *pb;
			GdkRectangle rc;
			ui_get_geometry(win,&rc);
			pb = gdk_pixbuf_scale_simple(bg_img,
										rc.width,
										rc.height,
										GDK_INTERP_BILINEAR);
			g_object_unref(bg_img);
			bg_img = pb;
		}
		g_free(p);

#ifdef ENABLE_GTK3
		cairo_surface_t *surface;
		cairo_pattern_t *pattern;
		surface=cairo_surface_create_from_pixbuf(root,bg_img);
		pattern=cairo_pattern_create_for_surface(surface);
		g_object_unref(bg_img);
		gdk_window_set_background_pattern(win,pattern);
		gdk_window_set_background_pattern(root,pattern);
		cairo_pattern_destroy(pattern);
#else
		GdkPixmap *pix = NULL;
		gdk_pixbuf_render_pixmap_and_mask(bg_img, &pix, NULL, 0);
		g_object_unref(bg_img);
		gdk_window_set_back_pixmap(win,pix,FALSE);
		gdk_window_set_back_pixmap(root,pix,FALSE);
		g_object_unref(pix);
#endif
	}
	else
	{
#ifdef ENABLE_GTK3
#if GTK_CHECK_VERSION(3,4,0)
		if(win) gdk_window_set_background_rgba(win,&bg_color);
		gdk_window_set_background_rgba(root,&bg_color);
#else
		if(win) gdk_window_set_background(win,&bg_color);
		gdk_window_set_background(root,&bg_color);
#endif	
#else
		GdkColormap *map;
		if(win)
		{
			map=(GdkColormap*)gdk_drawable_get_colormap(win);
			gdk_colormap_alloc_color(map, &bg_color, TRUE, TRUE);
			gdk_window_set_background(win, &bg_color);
		}
		map=(GdkColormap*)gdk_drawable_get_colormap(root);
		gdk_colormap_alloc_color(map, &bg_color, TRUE, TRUE);
		gdk_window_set_background(root, &bg_color);
#endif
	}
}

void ui_set_focus(GdkWindow *win)
{
#if GTK_CHECK_VERSION(2,24,0)
	Display *dpy=gdk_x11_display_get_xdisplay(gdk_window_get_display(win));
#else
	Display *dpy=gdk_x11_display_get_xdisplay(gdk_display_get_default());
#endif
	gdk_flush();
	while(1)
	{
		XWindowAttributes attr;
    	XGetWindowAttributes(dpy,GDK_WINDOW_XID(win),&attr);
    	if(attr.map_state == IsViewable) break;
    	usleep(10000);
	}
	XSetInputFocus(dpy,GDK_WINDOW_XID(win),RevertToNone,CurrentTime);
}

void ui_add_cursor(void)
{
    GdkCursor *cur;
    GdkWindow *root=gdk_get_default_root_window();
    cur = gdk_cursor_new(GDK_LEFT_PTR);
    gdk_window_set_cursor(root, cur);
    XDefineCursor(gdk_x11_get_default_xdisplay(),
    	GDK_WINDOW_XID(gdk_get_default_root_window()),
    	GDK_CURSOR_XCURSOR(cur));
#if GTK_CHECK_VERSION(3,0,0)
	g_object_unref(cur);
#else
    gdk_cursor_unref(cur);
#endif
}

void ui_set_cursor(GdkWindow *win,int which)
{
	GdkCursor *cursor=gdk_cursor_new(which);
	gdk_window_set_cursor (win,cursor);
#if GTK_CHECK_VERSION(3,0,0)
	g_object_unref(cursor);
#else
	gdk_cursor_unref(cursor);
#endif
}
