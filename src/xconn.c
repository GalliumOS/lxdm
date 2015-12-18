#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>

#ifdef LXDM_XCONN_XLIB
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <setjmp.h>
#endif

#ifdef LXDM_XCONN_XCB
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#endif

#ifdef LXDM_XCONN_XLIB

typedef Display *xconn_t;

static int CatchErrors(Display *dpy, XErrorEvent *ev)
{
	return 0;
}

static jmp_buf XErrEnv;
static int CatchIOErrors(Display *dpy)
{
	close(ConnectionNumber(dpy));
	longjmp(XErrEnv,1);
	return 0;
}

xconn_t xconn_open(const char *display)
{
	return XOpenDisplay(display);
}

void xconn_close(xconn_t c)
{
	if(!c) return;
	XSetErrorHandler(CatchErrors);
	XSetIOErrorHandler(CatchIOErrors);
	if(!setjmp(XErrEnv))
		XCloseDisplay(c);
	XSetErrorHandler(NULL);
	XSetIOErrorHandler(NULL);
}

void xconn_clean(xconn_t c)
{
	Window dummy, parent;
	Window *children;
	unsigned int nchildren;
	unsigned int i;
	Window Root;

	if(!c) return;

	XSetErrorHandler(CatchErrors);
	XSetIOErrorHandler(CatchIOErrors);

	Root = DefaultRootWindow(c);

	nchildren = 0;
	if(!setjmp(XErrEnv))
		XQueryTree(c, Root, &dummy, &parent, &children, &nchildren);
	else
		goto out;
	for( i = 0; i < nchildren; i++ )
	{
		if(!setjmp(XErrEnv))
		XKillClient(c, children[i]);
	}
	XFree((char *)children);
	if(!setjmp(XErrEnv))
		XSync(c, 0);
out:
	XSetErrorHandler(NULL);
	XSetIOErrorHandler(NULL);
}

#endif

#ifdef LXDM_XCONN_XCB

typedef struct{
	guint id;
	xcb_connection_t *c;
}*xconn_t;

typedef struct _XConnSource
{
	GSource source;
	GPollFD poll;
}XConnSource;

typedef gboolean (*XConnFunc)(gpointer data,xcb_generic_event_t *event);

static gboolean xconn_prepare (GSource *source,gint *timeout)
{
	*timeout=-1;
	return FALSE;
}

static gboolean xconn_check(GSource *source)
{
	XConnSource *s=(XConnSource*)source;
	if((s->poll.revents & G_IO_IN))
		return TRUE;
	return FALSE;
}

static gboolean xconn_dispatch (GSource *source,GSourceFunc callback,gpointer user_data)
{
	xconn_t c=user_data;
	xcb_generic_event_t *event;
	while((event=xcb_poll_for_event(c->c))!=NULL)
	{
		((XConnFunc)callback)(user_data,event);
		free(event);
	}
	return TRUE;
}

static GSourceFuncs xconn_funcs =
{
 	xconn_prepare,
	xconn_check,
	xconn_dispatch,
	NULL
};

static gboolean xconn_func(gpointer data,xcb_generic_event_t *event)
{
	return TRUE;
}


xconn_t xconn_open(const char *display)
{
	XConnSource *s;
	xcb_connection_t *dpy;
	xconn_t c;
	int fd;
	dpy=xcb_connect(display,0);
	/* is error in setup stage, there is memory leak at xcb */
	if(!dpy || xcb_connection_has_error(dpy))
		return NULL;
	c=malloc(sizeof(*c));
	c->c=dpy;
	fd=xcb_get_file_descriptor(dpy);
	s=(XConnSource*)g_source_new(&xconn_funcs,sizeof(XConnSource));
	g_source_set_callback((GSource*)s,(GSourceFunc)xconn_func,c,NULL);

	s->poll.fd=fd;
	s->poll.events=G_IO_IN;
	g_source_add_poll((GSource*)s,&s->poll);
	c->id=g_source_attach((GSource*)s,NULL);
	return c;
}

void xconn_close(xconn_t c)
{
	if(!c) return;
	g_source_remove(c->id);
	/* hack, clear the xcb has_error, so we can free it any way */
	if(xcb_connection_has_error(c->c) && *(int*)c->c==1)
		*(int*)c->c=0;
	xcb_disconnect(c->c);
	free(c);
}

#if 0
static xcb_window_t xconn_get_root(xconn_t c)
{
	const xcb_setup_t *setup;
	setup=xcb_get_setup(c->c);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator (setup);
	xcb_screen_t *screen = iter.data;
	return screen->root;
}
#endif

void xconn_clean(xconn_t c)
{
#if 0
	xcb_query_tree_cookie_t wintree;
	xcb_query_tree_reply_t *rep;
	xcb_window_t *children;
	xcb_window_t root;
	int i,len;
	if(!c) return;
	root=xconn_get_root(c);
	wintree = xcb_query_tree(c->c, root);
	rep = xcb_query_tree_reply(c->c, wintree, 0);
	if(!rep) return;
	len = xcb_query_tree_children_length(rep);
	children = xcb_query_tree_children(rep);
	for(i=0;i<len;i++)
		xcb_kill_client(c->c,children[i]);
	free(rep);
	xcb_flush(c->c);
#endif
}

#endif

