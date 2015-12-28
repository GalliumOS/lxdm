/*
 *      ui.c - basic ui of lxdm
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


#include <string.h>
#include <poll.h>
#include <grp.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>

#include <sys/wait.h>

#include "lxdm.h"
#include "lxcom.h"
#include "auth.h"

static pid_t greeter = -1;
static int greeter_pipe[2];
static GIOChannel *greeter_io;
static guint io_id;
static int user;
static gboolean first=TRUE;

static void xwrite(int fd,const void *buf,size_t size)
{
	int ret;
	do{
		ret=write(fd,buf,size);
	}while(ret==-1 && errno==EINTR);
}

static void ui_reset(void)
{
	xwrite(greeter_pipe[0], "reset\n", 6);
}

void ui_drop(void)
{
	/* if greeter, do quit */
	if( greeter > 0 )
	{
		lxcom_del_child_watch(greeter);
		xwrite(greeter_pipe[0], "exit\n", 5);
		g_source_remove(io_id);
		io_id = 0;
		g_io_channel_unref(greeter_io);
		greeter_io = NULL;
		close(greeter_pipe[1]);
		close(greeter_pipe[0]);
		waitpid(greeter, 0, 0) ;
		greeter=-1;
		user=-1;
	}
	if(io_id>0)
	{
		g_source_remove(io_id);
		io_id = 0;
		g_io_channel_unref(greeter_io);
		greeter_io = NULL;
		close(greeter_pipe[1]);
		close(greeter_pipe[0]);
	}
}

static void greeter_setup(void *userdata)
{
	struct passwd *pw=userdata;
	if(!pw)
	{
		return;
	}
	initgroups(pw->pw_name, pw->pw_gid);
	setgid(pw->pw_gid);
	setuid(pw->pw_uid);
}

static gchar *greeter_param(char *str, char *name)
{
	char *temp, *p;
	char ret[128];
	int i;
	temp = g_strdup_printf(" %s=", name);
	p = strstr(str, temp);
	if( !p )
	{
		g_free(temp);
		return NULL;
	}
	p += strlen(temp);
	g_free(temp);
	for( i = 0; i < 127; i++ )
	{
		if( !p[i] || isspace(p[i]) )
			break;
		ret[i] = p[i];
	}
	ret[i] = 0;
	if(!strcmp(name,"pass"))
	{
		gsize outlen;
		temp=(char*)g_base64_decode(ret,&outlen);
		if(!temp) return NULL;
		p=g_malloc(outlen+1);
		memcpy(p,temp,outlen);
		p[outlen]=0;
		g_free(temp);
		return p;
	}
	return g_strdup(ret);
}

static gboolean on_greeter_input(GIOChannel *source, GIOCondition condition, gpointer data)
{
	GIOStatus ret;
	char *str;

	if( !(G_IO_IN & condition) )
		return FALSE;
	ret = g_io_channel_read_line(source, &str, NULL, NULL, NULL);
	if( ret != G_IO_STATUS_NORMAL )
		return FALSE;

	if( !strncmp(str, "reboot", 6) )
		lxdm_do_reboot();
	else if( !strncmp(str, "shutdown", 6) )
		lxdm_do_shutdown();
	else if( !strncmp(str, "log ", 4) )
		g_message("%s",str + 4);
	else if( !strncmp(str, "login ", 6) )
	{
		char *user = greeter_param(str, "user");
		char *pass = greeter_param(str, "pass");
		char *session = greeter_param(str, "session");
		char *lang = greeter_param(str, "lang");
		if( user/* && pass */)
		{
			struct passwd *pw;
			int ret = lxdm_auth_user(AUTH_TYPE_NORMAL, user, pass, &pw);
			if( AUTH_SUCCESS == ret && pw != NULL )
			{
				ui_drop();
				lxdm_do_login(pw, session, lang,NULL);
			}
			else
			{
				if(pass!=NULL)
					xwrite(greeter_pipe[0], "reset\n", 6);
				else
					xwrite(greeter_pipe[0], "password\n", 9);
			}
		}
		g_free(user);
		g_free(pass);
		g_free(session);
		g_free(lang);
	}
	else if(!strncmp(str, "autologin ", 10))
	{
		char *user=g_key_file_get_string(config,"base","autologin",NULL);
		char *pass=g_key_file_get_string(config,"base","password",NULL);
		char *session = greeter_param(str, "session");
		char *lang = greeter_param(str, "lang");

		if(user)
		{
			struct passwd *pw;
			int ret = lxdm_auth_user(AUTH_TYPE_AUTO_LOGIN, user, pass, &pw);
			if( AUTH_SUCCESS == ret && pw != NULL )
			{
				ui_drop();
				lxdm_do_login(pw, session, lang,NULL);
			}
			else
			{
				ui_reset();
			}
		}
		else
		{
			ui_reset();
		}
			
		g_free(user);
		g_free(pass);
		g_free(session);
		g_free(lang);
	}
	g_free(str);
	return TRUE;
}

static void on_greeter_exit(void *data,int pid, int status)
{
	if( pid != greeter )
		return;
	greeter = -1;
}

int ui_greeter_user(void)
{
	return user;
}

void ui_prepare(void)
{
	char *p;

	if(greeter>0)
		return;

	/* if find greeter, run it */
	p = g_key_file_get_string(config, "base", "greeter", NULL);
	if( p && p[0] )
	{
		char *argv[3]={p,NULL,NULL};
		gboolean ret;
		struct passwd *pw;
		if(first)
		{
			char *t=g_key_file_get_string(config,"base","autologin",NULL);
			if(t && !strchr(t,' '))
				argv[1]="--auto-login";
			g_free(t);
			first=FALSE;
		}
		pw=getpwnam("lxdm");endpwent();
		ret = g_spawn_async_with_pipes(NULL, argv, NULL,
				   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,greeter_setup, pw,
				   &greeter, greeter_pipe + 0, greeter_pipe + 1, NULL, NULL);
		if( ret == TRUE )
		{
			g_free(p);
			greeter_io = g_io_channel_unix_new(greeter_pipe[1]);
			io_id = g_io_add_watch(greeter_io, G_IO_IN | G_IO_HUP | G_IO_ERR,
								   on_greeter_input, NULL);
			lxcom_add_child_watch(greeter, on_greeter_exit, 0);
			user=pw?pw->pw_uid:0;
			return;
		}
	}
	g_free(p);
	first=FALSE;
}

int ui_main(void)
{
	GMainLoop *loop = g_main_loop_new(NULL, 0);
	if(greeter>0)
		g_spawn_command_line_async("/etc/lxdm/LoginReady",NULL);
	g_main_loop_run(loop);
	return 0;
}
