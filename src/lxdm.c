/*
 *      lxdm.c - main entry of lxdm
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

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <shadow.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <sys/wait.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <sys/vt.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <execinfo.h>

#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif

#if HAVE_LIBCK_CONNECTOR
#include <ck-connector.h>
#endif

#ifndef HAVE_LIBPAM
#ifdef USE_PAM
#define HAVE_LIBPAM 1
#else
#define HAVE_LIBPAM 0
#endif
#endif

#if HAVE_LIBPAM
#include <security/pam_appl.h>
#endif

#include "lxdm.h"
#include "lxcom.h"
#include "xconn.h"
#include "lxcommon.h"
#include "auth.h"

#define LOGFILE "/var/log/lxdm.log"

typedef struct{
	gboolean idle;
	gboolean greeter;
	int tty;
	pid_t server;
	pid_t child;
	uid_t user;
	int display;
	char *option;	/* hold option in config file */
	xconn_t dpy;	/* hold this, or X crack */
	LXDM_AUTH auth;
#if HAVE_LIBCK_CONNECTOR
	CkConnector *ckc;
#endif
#ifndef DISABLE_XAUTH
	char mcookie[16];
#endif
	char **env;
}LXSession;

GKeyFile *config;
static int old_tty=1,def_tty = 7,nr_tty=0;
static int def_display=0;
static GSList *session_list;

static void lxdm_startx(LXSession *s);

static int get_active_vt(void)
{
	int console_fd;
	struct vt_stat console_state = { 0 };

	console_fd = open("/dev/console", O_RDONLY | O_NOCTTY);

	if( console_fd < 0 )
		goto out;

	if( ioctl(console_fd, VT_GETSTATE, &console_state) < 0 )
		goto out;

out:
	if( console_fd >= 0 )
		close(console_fd);

	return console_state.v_active;
}

static void set_active_vt(int vt)
{
	int fd;

	fd = open("/dev/console", O_RDWR);
	if( fd < 0 )
		fd = 0;
	ioctl(fd, VT_ACTIVATE, vt);
	if(fd!=0)
		close(fd);
}

void stop_pid(int pid)
{
    if( pid <= 0 ) return;
    lxcom_del_child_watch(pid);
    if( killpg(pid, SIGTERM) < 0 )
        killpg(pid, SIGKILL);
    if( kill(pid, 0) == 0 )
    {
        if( kill(pid, SIGTERM) )
            kill(pid, SIGKILL);
    }
    while( 1 )
    {
        int wpid, status;
        wpid = waitpid(pid,&status,0);
        if(pid == wpid)
            break;
	if(wpid<0 && errno!=EINTR)
		break;
    }

    while( waitpid(-1, 0, WNOHANG) > 0 ) ;
}

static LXSession *lxsession_find_greeter(void)
{
	GSList *p;
	for(p=session_list;p!=NULL;p=p->next)
	{
		LXSession *s=p->data;
		if(s->greeter) return s;
	}
	return NULL;
}

static LXSession *lxsession_find_idle(void)
{
	GSList *p;
	for(p=session_list;p!=NULL;p=p->next)
	{
		LXSession *s=p->data;
		if(s->idle) return s;
	}
	return NULL;
}

static LXSession *lxsession_find_user(uid_t user)
{
	GSList *p;
	for(p=session_list;p!=NULL;p=p->next)
	{
		LXSession *s=p->data;
		if(s->greeter) continue;
		if(s->user==user) return s;
	}
	return NULL;
}

LXSession *lxsession_find_tty(int tty)
{
	GSList *p;
	for(p=session_list;p!=NULL;p=p->next)
	{
		LXSession *s=p->data;
		if(s->tty==tty) return s;
	}
	return NULL;
}

static gboolean lxsession_get_active(void)
{
	GSList *p;
	for(p=session_list;p!=NULL;p=p->next)
	{
		LXSession *s=p->data;
		if(s->greeter || s->idle)
			continue;
		return TRUE;
	}
	return FALSE;
}

static void lxsession_set_active(LXSession *s)
{
	if(!s || s->tty<=0) return;
	if(get_active_vt()==s->tty)
		return;
	set_active_vt(s->tty);
}

static gboolean tty_is_used(int tty)
{
	GSList *p;
	for(p=session_list;p!=NULL;p=p->next)
	{
		LXSession *s=p->data;
		if(s->tty==tty)
			return TRUE;
	}
	return FALSE;
}

static gboolean display_is_used(int display)
{
	GSList *p;
	for(p=session_list;p!=NULL;p=p->next)
	{
		LXSession *s=p->data;
		if(s->display==display)
			return TRUE;
	}
	return FALSE;
}

static int lxsession_alloc_tty(void)
{
	int i;
	if(!tty_is_used(def_tty))
		return def_tty;
	for(i=7;i<13;i++)
	{
		if(!tty_is_used(i))
			return i;
	}
	return 0;
}

static int lxsession_alloc_display(void)
{
	int i;
	for(i=def_display;i<11;i++)
	{
		if(!display_is_used(i))
			return i;
	}
	return -1;
}

static LXSession *lxsession_add(void)
{
	LXSession *s;
	
	s=lxsession_find_idle();
	if(s) return s;
	if(g_slist_length(session_list)>=4)
		return NULL;
	s=g_new0(LXSession,1);
	s->greeter=FALSE;
	s->tty=lxsession_alloc_tty();
	s->display=lxsession_alloc_display();
	s->idle=TRUE;
	if(!s->tty || s->display<0)
	{
		g_message("alloc tty s->tty, display %d fail\n",s->display);
		g_free(s);
		return NULL;
	}
	s->env=NULL;
	lxdm_auth_init(&s->auth);
	session_list=g_slist_prepend(session_list,s);
	lxdm_startx(s);
	return s;
}

static LXSession *lxsession_greeter(void)
{
	char temp[16];
	LXSession *s;
	s=lxsession_find_greeter();
	if(s)
	{
		g_message("find prev greeter\n");
		lxsession_set_active(s);
		return s;
	}
	g_message("find greeter %p\n",s);
	s=lxsession_find_idle();
	g_message("find idle %p\n",s);
	if(!s) s=lxsession_add();
	g_message("add %p\n",s);
	if(!s)
	{
		g_warning("add new fail\n");
		return NULL;
	}
	s->greeter=TRUE;
	s->idle=FALSE;
	sprintf(temp,":%d",s->display);
	setenv("DISPLAY",temp,1);
	g_message("prepare greeter on %s\n",temp);
	ui_prepare();
	lxsession_set_active(s);
	g_message("start greeter on %s\n",temp);
	return s;
}

static void lxsession_stop(LXSession *s)
{
	if(s->greeter)
	{
		ui_drop();
		s->greeter=FALSE;
	}
	if(s->child>0)
	{
		lxcom_del_child_watch(s->child);
		killpg(s->child, SIGHUP);
		stop_pid(s->child);
		s->child = -1;
	}
	if( s->server > 0 )
	{
		xconn_clean(s->dpy);
	}
	lxdm_auth_session_end(&s->auth);
#if HAVE_LIBCK_CONNECTOR
	if( s->ckc != NULL )
	{
		DBusError error;
		dbus_error_init(&error);
		ck_connector_close_session(s->ckc, &error);
		if(dbus_error_is_set(&error))
		{
			dbus_error_free(&error);
		}
		ck_connector_unref(s->ckc);
		s->ckc=NULL;
	}
#endif
	s->idle=TRUE;
}

static void lxsession_free(LXSession *s)
{
	if(!s)
		return;
	session_list=g_slist_remove(session_list,s);
	lxsession_stop(s);
	if(s->server)
	{
		if(s->dpy)
		{
			xconn_close(s->dpy);
			s->dpy=NULL;
		}
		stop_pid(s->server);
		s->server=0;
	}
	g_free(s->option);
	g_strfreev(s->env);
	g_free(s);
}

static gboolean plymouth_is_running(void)
{
	int status;
	gboolean res;

	res=g_spawn_command_line_sync ("/bin/plymouth --ping",NULL,NULL,&status,NULL);
	if(!res) return FALSE;
	return WIFEXITED (status) && WEXITSTATUS (status) == 0;
}

static void plymouth_quit_with_transition(void)
{
	g_spawn_command_line_sync("/bin/plymouth quit --retain-splash",NULL,NULL,NULL,NULL);
}

static void plymouth_quit_without_transition(void)
{
	g_spawn_command_line_sync("/bin/plymouth quit",NULL,NULL,NULL,NULL);
}

static void plymouth_prepare_transition(void)
{
	g_spawn_command_line_sync ("/bin/plymouth deactivate",NULL,NULL,NULL,NULL);
}

static char *lxsession_xserver_command(LXSession *s)
{
	char *p;
	int arc;
	char **arg;
	int i;
	int novtswitch=0;
	
	if(s->option)
	{
		p=g_key_file_get_string(config,s->option,"xarg",0);
		if(p) return p;
	}

	p=g_key_file_get_string(config, "server", "arg", 0);
	if(!p) p=g_strdup("/usr/bin/X");	
	g_shell_parse_argv(p, &arc, &arg, 0);
	g_free(p);
	for(i=1;i<arc;)
	{
		p = arg[i];
		if(!strncmp(p, "vt", 2) && isdigit(p[2]) &&
			( !p[3] || (isdigit(p[3]) && !p[4]) ) )
		{
			g_free(arg[i]);
			arc--;memcpy(arg+i,arg+i+1,(arc-i)*sizeof(char*));
		}
		else if(!strcmp(p,"-background"))
		{
			g_free(arg[i]);
			arc--;memcpy(arg+i,arg+i+1,(arc-i)*sizeof(char*));
			if(i<arc && !strcmp(arg[i],"none"))
			{
				g_free(arg[i]);
				arc--;memcpy(arg+i,arg+i+1,(arc-i)*sizeof(char*));
			}
		}
		else if(!strcmp(p,"-nr"))
		{
			g_free(arg[i]);
			arc--;memcpy(arg+i,arg+i+1,(arc-i)*sizeof(char*));
		}
		else if(!strcmp(p,"-novtswitch"))
		{
			novtswitch=1;
		}
		else
		{
			i++;
		}
	}

	arg = g_renew(char *, arg, arc + 10);
	if(nr_tty)
	{
		arg[arc++] = g_strdup("-background");
		arg[arc++] = g_strdup("none");
	}
	arg[arc++] = g_strdup_printf(":%d",s->display);
	if(s->tty>0)
		arg[arc++] = g_strdup_printf("vt%02d", s->tty);
	if(g_key_file_get_integer(config,"server","tcp_listen",0)!=1)
	{
		arg[arc++] = g_strdup("-nolisten");
		arg[arc++] = g_strdup("tcp");
	}
	if(!novtswitch)
	{
		arg[arc++] = g_strdup("-novtswitch");
	}
	arg[arc] = NULL;
	p=g_strjoinv(" ", arg);
	g_strfreev(arg);
	return p;
}

void lxdm_get_tty(void)
{
	char *s = g_key_file_get_string(config, "server", "arg", 0);
	int arc;
	char **arg;
	int len;
	int gotvtarg = 0;
	gboolean plymouth;
    
	plymouth=plymouth_is_running();
	if(plymouth)
	{
		g_message("found plymouth running\n");
		plymouth_prepare_transition();
	}

	old_tty=get_active_vt();
	if( !s ) s = g_strdup("/usr/bin/X");
	g_shell_parse_argv(s, &arc, &arg, 0);
	g_free(s);
	for( len = 0; arg && arg[len]; len++ )
	{
		char *p = arg[len];
		if( !strncmp(p, "vt", 2) && isdigit(p[2]) &&
		( !p[3] || (isdigit(p[3]) && !p[4]) ) )
		{
			def_tty = atoi(p + 2);
			gotvtarg = 1;
		}
		else if(!strcmp(p,"-background") || !strcmp(p,"-nr"))
		{
			nr_tty=1;
		}
		else if(p[0]==':' && isdigit(p[1]))
		{
			def_display=atoi(p+1);
		}
	}
	if(!gotvtarg)
	{
		/* support plymouth */
		if(g_key_file_get_integer(config, "server", "active_vt", 0) )
			/* use the active vt */
			def_tty = old_tty;
		if(plymouth)
		{
			nr_tty=1;
			plymouth_quit_with_transition();
		}
	}
	else
	{
		if(plymouth) /* set tty and plymouth running */
			plymouth_quit_without_transition();
	}
	g_strfreev(arg);
}

void lxdm_quit_self(int code)
{
	g_message("quit code %d\n",code);
	exit(code);
}

static void log_init(void)
{
	int fd_log;
	g_unlink(LOGFILE ".old");
	g_rename(LOGFILE, LOGFILE ".old");
	fd_log = open(LOGFILE, O_CREAT|O_APPEND|O_TRUNC|O_WRONLY|O_EXCL, 0640);
	if(fd_log == -1) return;
	dup2(fd_log, 1);
	dup2(fd_log, 2);
	close(fd_log);
}

static void log_ignore(const gchar *log_domain, GLogLevelFlags log_level,
                       const gchar *message, gpointer user_data)
{
}
#if 0
GSList *do_scan_xsessions(void)
{
    GSList *xsessions = NULL;
    GDir *d;
    const char *basename;
    GKeyFile *f;

    d = g_dir_open(XSESSIONS_DIR, 0, NULL);
    if( !d )
        return NULL;

    f = g_key_file_new();
    while( ( basename = g_dir_read_name(d) ) != NULL )
    {
        char *file_path;
        gboolean loaded;

        if(!g_str_has_suffix(basename, ".desktop"))
            continue;

        file_path = g_build_filename(XSESSIONS_DIR, basename, NULL);
        loaded = g_key_file_load_from_file(f, file_path, G_KEY_FILE_NONE, NULL);
        g_free(file_path);

        if( loaded )
        {
            char *name = g_key_file_get_locale_string(f, "Desktop Entry", "Name", NULL, NULL);
            if( name )
            {
                char *exec = g_key_file_get_string(f, "Desktop Entry", "Exec", NULL);
                if(exec)
                {
                    Session* sess = g_new( Session, 1 );
                    sess->name = name;
                    sess->exec = exec;
                    sess->desktop_file = g_strdup(basename);
                    if( !strcmp(name, "LXDE") )
                        xsessions = g_slist_prepend(xsessions, sess);
                    else
                        xsessions = g_slist_append(xsessions, sess);
                    continue; /* load next file */
                    g_free(exec);
                }
                g_free(name);
            }
        }
    }
    g_dir_close(d);
    g_key_file_free(f);
    return xsessions;
}

void free_xsessions(GSList *l)
{
    GSList *p;
    Session *sess;

    for( p = l; p; p = p->next )
    {
        sess = p->data;
        g_free(sess->name);
        g_free(sess->exec);
        g_free(sess);
    }
    g_slist_free(l);
}
#endif

#ifndef DISABLE_XAUTH

static inline void xauth_write_uint16(int fd,uint16_t data)
{
	uint8_t t;
	t=data>>8;
	write(fd,&t,1);
	t=data&0xff;
	write(fd,&t,1);
}

static inline void xauth_write_string(int fd,const char *s)
{
	size_t len=strlen(s);
	xauth_write_uint16(fd,(uint16_t)len);
	write(fd,s,len);
}

static void xauth_write_file(const char *file,int dpy,char data[16])
{
	int fd;
	char addr[128];
	char buf[16];
	
	sprintf(buf,"%d",dpy);
	gethostname(addr,sizeof(addr));
	
	fd=open(file,O_CREAT|O_TRUNC|O_WRONLY,0600);
	if(!fd==-1) return;
	xauth_write_uint16(fd,256);		//FamilyLocalHost
	xauth_write_string(fd,addr);
	xauth_write_string(fd,buf);
	xauth_write_string(fd,"MIT-MAGIC-COOKIE-1");
	xauth_write_uint16(fd,16);
	write(fd,data,16);
	close(fd);
}

static void create_server_auth(LXSession *s)
{
	GRand *h;
	int i;
	char *authfile;

	h = g_rand_new();
	for( i = 0; i < 16; i++ )
	{
		s->mcookie[i] = g_rand_int(h)&0xff;
	}
	g_rand_free(h);

	authfile = g_strdup_printf("/var/run/lxdm/lxdm-:%d.auth",s->display);

	//setenv("XAUTHORITY",authfile,1);
	remove(authfile);
	xauth_write_file(authfile,s->display,s->mcookie);
	g_free(authfile);
}

static char ** create_client_auth(struct passwd *pw,char **env)
{
	LXSession *s;
	char *authfile;
	
	if(pw->pw_uid==0) /* root don't need it */
		return env;
        
	s=lxsession_find_user(pw->pw_uid);
	if(!s)
		return env;
	
	/* pam_mktemp may provide XAUTHORITY to DM, just use it */
	if((authfile=(char*)g_environ_getenv(env,"XAUTHORITY"))!=NULL)
	{
		authfile=g_strdup(authfile);
	}
	else
	{
		char *path;
		path=g_key_file_get_string(config,"base","xauth_path",NULL);
		if(path)
		{
			authfile = g_strdup_printf("%s/.Xauth%d", path,pw->pw_uid);
			g_free(path);
		}
		else
		{
			authfile = g_strdup_printf("%s/.Xauthority", pw->pw_dir);
		}
	}
	remove(authfile);
	xauth_write_file(authfile,s->display,s->mcookie);
	env=g_environ_setenv(env,"XAUTHORITY",authfile,TRUE);
	chown(authfile,pw->pw_uid,pw->pw_gid);
	g_free(authfile);
	
	return env;
}
#endif

int lxdm_auth_user(int type,char *user, char *pass, struct passwd **ppw)
{
    LXSession *s;
    int ret;
    s=lxsession_find_greeter();
    if(!s) s=lxsession_find_idle();
    if(!s) s=lxsession_add();
    if(!s)
    {
        g_critical("lxsession_add fail\n");
        exit(0);
    }
	ret=lxdm_auth_user_authenticate(&s->auth,user,pass,type);
	if(ret==AUTH_SUCCESS)
		*ppw=&s->auth.pw;
	return ret;
}

static void close_left_fds(void)
{
	struct dirent **list;
	char path[256];
	int n;

	snprintf(path,sizeof(path),"/proc/%d/fd",getpid());
	n=scandir(path,&list,0,0);
	if(n<0) return;
	while(n--)
	{
		int fd=atoi(list[n]->d_name);
		free(list[n]);
		if(fd<=STDERR_FILENO)
			continue;
		close(fd);
	}
	free(list);
	
	int fd = open("/dev/null", O_WRONLY);
	if(fd == -1) return;
	dup2(fd, 1);
	dup2(fd, 2);
	close(fd);
}

void switch_user(struct passwd *pw, const char *run, char **env)
{
	int fd;
    
	setenv("USER",pw->pw_name,1);
	setenv("LOGNAME",pw->pw_name,1);
	setenv("SHELL",pw->pw_shell,1);
	setenv("HOME",pw->pw_dir,1);

	g_spawn_command_line_sync ("/etc/lxdm/PreLogin",NULL,NULL,NULL,NULL);

	if( !pw || initgroups(pw->pw_name, pw->pw_gid) ||
			setgid(pw->pw_gid) || setuid(pw->pw_uid) || setsid() == -1 )
		exit(EXIT_FAILURE);
	chdir(pw->pw_dir);
	fd=open(".xsession-errors",O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR);
	if(fd!=-1)
	{
		dup2(fd,STDERR_FILENO);
		close(fd);
	}

	/* reset signal */
	signal(SIGCHLD, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGALRM, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	close_left_fds();

	g_spawn_command_line_async ("/etc/lxdm/PostLogin",NULL);
	execle("/etc/lxdm/Xsession", "/etc/lxdm/Xsession", run, NULL, env);
	exit(EXIT_FAILURE);
}

static void get_lock(void)
{
	FILE *fp;
	char *lockfile;

	lockfile = g_key_file_get_string(config, "base", "lock", 0);
	if( !lockfile ) lockfile = g_strdup("/var/run/lxdm.pid");

	fp = fopen(lockfile, "r");
	if( fp )
	{
		int pid;
		int ret;
		ret = fscanf(fp, "%d", &pid);
		fclose(fp);
		if(ret == 1 && pid!=getpid())
		{
			if(kill(pid, 0) == 0 || (ret == -1 && errno == EPERM))
			{
				/* we should only quit if the pid running is lxdm */
#ifdef __linux__
				char path[64],buf[128];
				sprintf(path,"/proc/%d/exe",pid);
				ret=readlink(path,buf,128);
				if(ret<128 && ret>0 && strstr(buf,"lxdm-binary"))
					lxdm_quit_self(1);
#else
				lxdm_quit_self(1);
#endif	
			}
		}
	}
	fp = fopen(lockfile, "w");
	if( !fp )
	{
		g_critical("open lock file %s fail\n",lockfile);
		lxdm_quit_self(0);
	}
	fprintf( fp, "%d", getpid() );
	fclose(fp);
	g_free(lockfile);
}

static void put_lock(void)
{
    FILE *fp;
    char *lockfile;

    lockfile = g_key_file_get_string(config, "base", "lock", 0);
    if( !lockfile ) lockfile = g_strdup("/var/run/lxdm.pid");
    fp = fopen(lockfile, "r");
    if( fp )
    {
        int pid;
        int ret;
        ret = fscanf(fp, "%d", &pid);
        fclose(fp);
        if( ret == 1 && pid == getpid() )
            remove(lockfile);
    }
    g_free(lockfile);
}

static void on_xserver_stop(void *data,int pid, int status)
{
	LXSession *s=data;
	LXSession *greeter;

	g_message("xserver stop, restart. return status %x\n",status);

	stop_pid(pid);
	s->server = -1;
	lxsession_stop(s);
	greeter=lxsession_find_greeter();
	if(s->greeter || !greeter)
	{
		s->greeter=TRUE;
		xconn_close(s->dpy);
		s->dpy=NULL;
		lxdm_startx(s);
		ui_drop();
		ui_prepare();
		lxsession_set_active(greeter);
	}
	else
	{
		lxsession_free(s);
		lxsession_set_active(greeter);
	}
}

void lxdm_startx(LXSession *s)
{
	char *arg;
	char **args;
	int i;
	char display[16];
	
	lxsession_set_active(s);
	
	sprintf(display,":%d",s->display);
	setenv("DISPLAY",display,1);

	#ifndef DISABLE_XAUTH
	create_server_auth(s);
	#endif

	arg = lxsession_xserver_command(s);
	args = g_strsplit(arg, " ", -1);
	g_free(arg);

	s->server = vfork();

	switch( s->server )
	{
	case 0:
		execvp(args[0], args);
		g_critical("exec %s fail\n",args[0]);
		lxdm_quit_self(0);
		break;
	case -1:
	/* fatal error, should not restart self */
		g_critical("fork proc fail\n");
		lxdm_quit_self(0);
		break;
	default:
		break;
	}
	g_strfreev(args);
	lxcom_add_child_watch(s->server, on_xserver_stop, s);

	g_message("%ld: add xserver watch\n",time(NULL));
	for( i = 0; i < 100; i++ )
	{
		if(lxcom_last_sig==SIGINT || lxcom_last_sig==SIGTERM)
			break;
		if((s->dpy=xconn_open(display))!=NULL)
			break;
		g_usleep(50 * 1000);
		//g_message("retry %d\n",i);
	}
	g_message("%ld: start xserver in %d retry",time(NULL),i);
	if(s->dpy==NULL)
		exit(EXIT_FAILURE);
	
	if(s->option && g_key_file_has_key(config,s->option,"numlock",NULL))
	{
		i=g_key_file_get_integer(config,s->option,"numlock",0);
		arg=g_strdup_printf("%s %d",LXDM_NUMLOCK_PATH,i);
		g_spawn_command_line_async(arg,NULL);
		g_free(arg);

	}
	else if(g_key_file_has_key(config,"base","numlock",NULL))
	{
		i=g_key_file_get_integer(config,"base","numlock",0);
		arg=g_strdup_printf("%s %d",LXDM_NUMLOCK_PATH,i);
		g_spawn_command_line_async(arg,NULL);
		g_free(arg);
	}
}

static void exit_cb(void)
{
	g_message("exit cb\n");
	ui_drop();
	while(session_list)
		lxsession_free(session_list->data);
	g_message("free session\n");
	put_lock();
	set_active_vt(old_tty);
	g_key_file_free(config);
}

static int get_run_level(void)
{
#if defined(HAVE_UTMPX_H) && defined(RUN_LVL)
	int res=0;
	struct utmpx *ut,tmp;

	setutxent();
	tmp.ut_type=RUN_LVL;
	ut=getutxid(&tmp);
	if(!ut)
	{
		endutxent();
		return 5;
	}
	res=ut->ut_pid & 0xff;
	endutxent();
	//g_message("runlevel %c\n",res);
	return res;
#else
	return 5;
#endif
}

static void on_session_stop(void *data,int pid, int status)
{
	int level;
	LXSession *s=data;

	lxsession_stop(s);

	level=get_run_level();
	if(level=='0' || level=='6')
	{
		if(level=='0')
			g_spawn_command_line_sync("/etc/lxdm/PreShutdown",0,0,0,0);
		else
			g_spawn_command_line_sync("/etc/lxdm/PreReboot",0,0,0,0);
		g_message("run level %c\n",level);
		lxdm_quit_self(0);
	}
	if(s!=lxsession_greeter())
	{
		lxsession_free(s);
	}
	else if(g_key_file_get_integer(config,"server","reset",NULL)==1)
	{
		lxsession_free(s);
		lxsession_greeter();
	}
	gchar *argv[] = { "/etc/lxdm/PostLogout", NULL };
	g_spawn_async(NULL, argv, s->env, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}

gboolean lxdm_get_session_info(char *session,char **pname,char **pexec)
{
	char *name=NULL,*exec=NULL;
	if(!session || !session[0])
	{
		name=g_key_file_get_string(config, "base", "session", 0);
		if(!name && getenv("PREFERRED"))
			name = g_strdup(getenv("PREFERRED"));
		if(!name && getenv("DESKTOP"))
			name = g_strdup(getenv("DESKTOP"));
		if(!name) name=g_strdup("LXDE");
	}
	else
	{
		char *p=strrchr(session,'.');
		if(p && !strcmp(p,".desktop"))
		{
			GKeyFile *cfg=g_key_file_new();
			if(!g_key_file_load_from_file(cfg,session,G_KEY_FILE_NONE,NULL))
			{
				g_key_file_free(cfg);
				return FALSE;
			}
			name=g_key_file_get_string(cfg,"Desktop Entry","Name",NULL);
			exec=g_key_file_get_string(cfg,"Desktop Entry","Exec",NULL);
			g_key_file_free(cfg);
			if(!name || !exec)
			{
				g_free(name);
				g_free(exec);
				return FALSE;
			}			
		}
		else
		{
			GKeyFile *f;
			char *file_path;
			gboolean loaded;

			f = g_key_file_new();
			char *desktop_name = g_strconcat(session, ".desktop", NULL);
			file_path = g_build_filename(XSESSIONS_DIR, desktop_name, NULL);
			loaded = g_key_file_load_from_file(f, file_path, G_KEY_FILE_NONE, NULL);
			g_free(file_path);
			g_free(desktop_name);

			if ( loaded )
			{
				name = g_key_file_get_locale_string(f, "Desktop Entry", "Name", NULL, NULL);
				exec = g_key_file_get_string(f, "Desktop Entry", "Exec", NULL);
			}
			else
			{
				name=g_strdup(session);
			}
			g_key_file_free(f);
		}
	}
	if(name && !exec)
	{
		if(!strcasecmp(name,"LXDE"))
			exec = g_strdup("startlxde");
		else if( !strcasecmp(name, "GNOME") )
			exec = g_strdup("gnome-session");
		else if( !strcasecmp(name, "KDE") )
			exec = g_strdup("startkde");
		else if( !strcasecmp(name, "XFCE") || !strcasecmp(name, "xfce4"))
			exec = g_strdup("startxfce4");
		else
			exec=g_strdup(name);
	}
	if(pname) *pname=name;
	if(pexec) *pexec=exec;
	return TRUE;
}

static void lxdm_save_login(char *session,char *lang)
{
	char *old;
	GKeyFile *var;
	int dirty=0;
	if(!session || !session[0])
		session="__default__";
	if(!lang)
		lang="";
	var=g_key_file_new();
	g_key_file_set_list_separator(var, ' ');
	g_key_file_load_from_file(var,VCONFIG_FILE,0,NULL);
	old=g_key_file_get_string(var,"base","last_session",0);
	if(0!=g_strcmp0(old,session))
	{
		g_key_file_set_string(var,"base","last_session",session);
		dirty++;
	}
	g_free(old);
	old=g_key_file_get_string(var,"base","last_lang",0);
	if(0!=g_strcmp0(old,lang))
	{
		g_key_file_set_string(var,"base","last_lang",lang);
		dirty++;
	}
	g_free(old);
	if(lang[0])
	{
		char **list;
		gsize len;
		list=g_key_file_get_string_list(var,"base","last_langs",&len,NULL);
		if(!list)
		{
			list=g_new0(char*,2);
			list[0]=g_strdup(lang);
			g_key_file_set_string_list(var,"base","last_langs",(void*)list,1);
			g_strfreev(list);
			dirty++;
		}
		else
		{
			int i;
			for(i=0;i<len;i++)
			{
				if(!strcmp(list[i],lang)) break;
			}
			if(i==len)
			{
				list=g_renew(char*,list,len+2);
				list[len]=g_strdup(lang);
				list[len+1]=NULL;
				g_key_file_set_string_list(var,"base","last_langs",(void*)list,len+1);
				dirty++;
			}
			g_strfreev(list);
		}
	}
	if(dirty)
	{
		gsize len;
        char* data = g_key_file_to_data(var, &len, NULL);
		mkdir("/var/lib/lxdm",0755);
		chmod("/var/lib/lxdm",0755);
        g_file_set_contents(VCONFIG_FILE, data, len, NULL);
        g_free(data);
	}
	g_key_file_free(var);
}

void lxdm_do_login(struct passwd *pw, char *session, char *lang, char *option)
{
	char *session_name=0,*session_exec=0;
	gboolean alloc_session=FALSE,alloc_lang=FALSE;
	int pid;
	LXSession *s,*prev;
	
	lxdm_save_login(session,lang);
	if(!strcmp(session,"__default__"))
		session=NULL;

	if(!session ||!session[0] || !lang || !lang[0])
	{
		char *path=g_strdup_printf("%s/.dmrc",pw->pw_dir);
		GKeyFile *dmrc=g_key_file_new();
		g_key_file_load_from_file(dmrc,path,G_KEY_FILE_NONE,0);
		g_free(path);
		if(!session || !session[0])
		{
			session=g_key_file_get_string(dmrc,"Desktop","Session",NULL);
			alloc_session=TRUE;
		}
		g_key_file_free(dmrc);
	}

	if(!lxdm_get_session_info(session,&session_name,&session_exec))
	{
		if(alloc_session)
			g_free(session);
		if(alloc_lang)
			g_free(lang);
		ui_prepare();
		g_debug("get session %s info fail\n",session);
		return;
	}

	g_debug("login user %s session %s lang %s\n",pw->pw_name,session_exec,lang);

	if( pw->pw_shell[0] == '\0' )
	{
		setusershell();
		strcpy(pw->pw_shell, getusershell());
		endusershell();
	}
	prev=lxsession_find_user(pw->pw_uid);
	s=lxsession_find_greeter();
	if(prev && prev->child>0)
	{
		if(s) lxsession_free(s);
		lxsession_set_active(prev);
		return;
	}
	if(!s) s=lxsession_find_idle();
	if(!s) s=lxsession_add();
	if(!s)
	{
		g_critical("lxsession_add fail\n");
		exit(0);
	}
	s->greeter=FALSE;
	s->idle=FALSE;
	s->user=pw->pw_uid;
	if(option)
		s->option=g_strdup(option);
#if HAVE_LIBCK_CONNECTOR
	if(s->ckc)
	{
		ck_connector_unref(s->ckc);
		s->ckc=NULL;
	}
#endif
	lxdm_auth_session_begin(&s->auth,session_name,s->tty,s->display,s->mcookie);
#if HAVE_LIBCK_CONNECTOR
#if HAVE_LIBPAM
	if(!s->ckc && (!s->auth.handle || !pam_getenv(s->auth.handle,"XDG_SESSION_COOKIE")))
#else
	if(!s->ckc)
#endif
	{
		s->ckc = ck_connector_new();
	}
	if( s->ckc != NULL )
	{
		DBusError error;
		char x[256], *d, *n;
		gboolean is_local=TRUE;
		sprintf(x, "/dev/tty%d", s->tty);
		dbus_error_init(&error);
		d = x; n = getenv("DISPLAY");
		if( ck_connector_open_session_with_parameters(s->ckc, &error,
							  "unix-user", &pw->pw_uid,
	// disable this, follow the gdm way 
							  //"display-device", &d,
							  "x11-display-device", &d,
							  "x11-display", &n,
							  "is-local",&is_local,
							  NULL))
		{
			setenv("XDG_SESSION_COOKIE", ck_connector_get_cookie(s->ckc), 1);
		}
		else
		{
			g_message("create ConsoleKit session fail\n");
		}
	}
	else
	{
		g_message("create ConsoleKit connector fail\n");
	}
#endif
	char **env, *path;
	env=g_get_environ();

	env=g_environ_setenv(env, "HOME", pw->pw_dir, TRUE);
	env=g_environ_setenv(env, "SHELL", pw->pw_shell, TRUE);
	env=g_environ_setenv(env, "USER", pw->pw_name, TRUE);
	env=g_environ_setenv(env, "LOGNAME", pw->pw_name, TRUE);

	/* override $PATH if needed */
	path = g_key_file_get_string(config, "base", "path", 0);
	if( G_UNLIKELY(path) && path[0] ) /* if PATH is specified in config file */
		env=g_environ_setenv(env, "PATH", path, TRUE); /* override current $PATH with config value */
	else if(!getenv("PATH")) /* if PATH is not set */
		env=g_environ_setenv(env, "PATH", "/usr/local/bin:/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/sbin", TRUE); /* set proper default */
	g_free(path);
	/* optionally override $LANG, $LC_MESSAGES, and $LANGUAGE */
	if( lang && lang[0] )
	{
		/* use this special environment variable to set the language related
		   env. variables from Xsession after ~/.profile has been sourced */
		env=g_environ_setenv(env, "GREETER_LANGUAGE", lang, TRUE);
	}
	
#ifndef DISABLE_XAUTH
	env=create_client_auth(pw,env);
#endif
	s->env = env;

	/*s->child = pid = fork();
	if(s->child==0)
	{
		env=lxdm_auth_append_env(&s->auth,env);
		lxdm_auth_clean_for_child(&s->auth);
		switch_user(pw, session_exec, env);
		lxdm_quit_self(4);
	}*/
	
	s->child = pid = lxdm_auth_session_run(&s->auth,session_exec,env);
	
	g_free(session_name);
	g_free(session_exec);
	if(alloc_session)
		g_free(session);
	if(alloc_lang)
		g_free(lang);
	lxcom_add_child_watch(pid, on_session_stop, s);
}

void lxdm_do_reboot(void)
{
	char *cmd;
	cmd = g_key_file_get_string(config, "cmd", "reboot", 0);
	if( !cmd ) cmd = g_strdup("reboot");
	g_spawn_command_line_sync("/etc/lxdm/PreReboot",0,0,0,0);
	g_spawn_command_line_async(cmd,0);
	g_free(cmd);
	lxdm_quit_self(0);
}

void lxdm_do_shutdown(void)
{
	char *cmd;
	cmd = g_key_file_get_string(config, "cmd", "shutdown", 0);
	if( !cmd ) cmd = g_strdup("shutdown -h now");
	g_spawn_command_line_sync("/etc/lxdm/PreShutdown",0,0,0,0);
	g_spawn_command_line_async(cmd,0);
	g_free(cmd);
	lxdm_quit_self(0);
}

static gboolean auto_login_future(void)
{
	return g_key_file_get_integer(config,"base","timeout",NULL)>=5;
}

int lxdm_do_auto_login(void)
{
	struct passwd *pw;
	char *p,**users;
	char *pass=NULL;
	int i,count,ret;
	int success=0;

	p = g_key_file_get_string(config, "base", "autologin", 0);
	if(!p) return 0;
	users=g_strsplit(p," ",8);
	g_free(p);
	count=g_strv_length(users);

	#ifdef ENABLE_PASSWORD
	if(count==1)
		pass = g_key_file_get_string(config, "base", "password", 0);
	#endif

	/* get defaults from last login */
	GKeyFile *var_config = g_key_file_new();
	g_key_file_set_list_separator(var_config, ' ');
	g_key_file_load_from_file(var_config,VCONFIG_FILE,G_KEY_FILE_KEEP_COMMENTS, NULL);

	char* last_session = g_key_file_get_string(var_config, "base", "last_session", NULL);
	if(last_session != NULL && last_session[0] == 0)
	{
		g_free(last_session);
		last_session = NULL;
	}

	char* last_lang = g_key_file_get_string(var_config, "base", "last_lang", NULL);

	g_key_file_free(var_config);

	for(i=0;i<count;i++)
	{
		char *user,*session=NULL,*lang=NULL,*option=NULL;
		p=users[i];
		/* autologin users starting with '@' get own config section with
		 * user=, session= and lang= entry
		 */
		if(p[0]=='@')	
		{
			option=p+1;
			user=g_key_file_get_string(config,option,"user",NULL);
			session=g_key_file_get_string(config,option,"session",0);
			lang=g_key_file_get_string(config,option,"lang",0);
		}
		/* autologin users not starting with '@' get user, session, lang section
                 * from last login
                 */
		else
		{
			user=g_strdup(p);
			session=g_strdup(last_session);
			lang=g_strdup(last_lang);
		}
		ret=lxdm_auth_user(AUTH_TYPE_AUTO_LOGIN, user, pass, &pw);
		if(ret==AUTH_SUCCESS)
		{
			lxdm_do_login(pw,session,lang,option);
			success=1;
		}
		g_free(user);g_free(session);g_free(lang);
	}
	g_free(last_lang);
	g_free(last_session);
	g_strfreev(users);
	g_free(pass);
	return success;
}

static void log_sigsegv(void)
{
	void *array[40];
	size_t size;
	char **bt_strs;
	size_t i;

	size=backtrace(array,40);
	bt_strs=backtrace_symbols(array, size);

	for (i=0; i<size; i++)
	    fprintf(stderr, "%s\n", bt_strs[i]);

	free(bt_strs);
}

static void sigsegv_handler(int sig)
{
	switch(sig){
	case SIGSEGV:
		log_sigsegv();
		lxdm_quit_self(0);
		break;
	default:
		break;
	}
}

static void lxdm_signal_handler(void *data,int sig)
{
	switch(sig){
	case SIGTERM:
	case SIGINT:
		g_critical("QUIT BY SIGNAL\n");
		lxdm_quit_self(0);
		break;
	default:
		break;
	}
}

static gboolean strv_find(char **strv,const char *p)
{
	int i;
	if(!strv || !p) return FALSE;
	for(i=0;strv[i]!=NULL;i++)
	{
		if(!strcmp(strv[i],p))
			return TRUE;
	}
	return FALSE;
}

static char *lxdm_get_user_face(struct passwd *pw)
{
	GKeyFile *kf;
	char *face;
	char *file;
	face=g_strdup_printf("%s/.face",pw->pw_dir);
	if(!access(face,R_OK))
		return face;
	g_free(face);
	kf=g_key_file_new();
	file=g_build_filename ("/var/lib/AccountsService/users/", pw->pw_name, NULL);
	g_key_file_load_from_file (kf, file, 0, NULL);
	g_free(file);
	face=g_key_file_get_string(kf,"User","Icon",NULL);
	g_key_file_free(kf);
	if(face && !access(face,R_OK))
		return face;
	g_free(face);
	return NULL;
}

GKeyFile *lxdm_user_list(void)
{
	struct passwd *pw;
	GKeyFile *kf;
	char *face;
	char **black=NULL;
	char **white=NULL;
	
	// load black list
	face=g_key_file_get_string(config,"userlist","black",NULL);
	if(face)
	{
		black=g_strsplit(face," ",-1);
		g_free(face);
	}
	//load white list
	face=g_key_file_get_string(config,"userlist","white",NULL);
	if(face)
	{
		white=g_strsplit(face," ",-1);
		g_free(face);
	}

	kf=g_key_file_new();
	g_key_file_set_comment(kf,NULL,NULL,"lxdm user list",NULL);
	while((pw=getpwent())!=NULL)
	{
		char *valid_shell;
		gboolean ret;
	
		if(strstr(pw->pw_shell, "nologin"))
			continue;

		ret = FALSE;
		setusershell();
		while ((valid_shell = getusershell()) != NULL) {
			if (g_strcmp0 (pw->pw_shell, valid_shell) != 0)
				continue;
			ret = TRUE;
		}
		endusershell();
		if(!ret)
			continue;

		if(strncmp(pw->pw_dir,"/home/",6))
		{
			if(!strv_find(white,pw->pw_name))
				continue;
		}
		else
		{
			if(strv_find(black,pw->pw_name))
				continue;
		}

		g_key_file_set_string(kf,pw->pw_name,"name",pw->pw_name);
		if(pw->pw_gecos && pw->pw_gecos[0] && strcmp(pw->pw_name,pw->pw_gecos))
			g_key_file_set_string(kf,pw->pw_name,"gecos",pw->pw_gecos);
		if(lxsession_find_user(pw->pw_uid))
			g_key_file_set_boolean(kf,pw->pw_name,"login",TRUE);
		face=lxdm_get_user_face(pw);
		if(face)
			g_key_file_set_string(kf,pw->pw_name,"face",face);
		g_free(face);
	}
	endpwent();
	if(black) g_strfreev(black);
	if(white) g_strfreev(white);
	return kf;
}

static GString *lxdm_user_cmd(void *data,int user,int arc,char **arg)
{
	LXSession *s=NULL;
	GString *res=NULL;
	s=lxsession_find_user(user);
	if(!s && user==ui_greeter_user())
		s=lxsession_find_greeter();
	g_message("greeter %d session %p\n",ui_greeter_user(),lxsession_find_greeter());
	g_message("user %d session %p cmd %s\n",user,s,arg[0]);
	if(user!=0 && s==NULL)
		return NULL;
	if(!strcmp(arg[0],"USER_SWITCH"))
	{
		g_message("start greeter\n");
		lxsession_greeter();
	}
	else if(!strcmp(arg[0],"USER_LIST"))
	{
		GKeyFile *kf=lxdm_user_list();
		gsize len;
		char *p=g_key_file_to_data(kf,&len,NULL);
		if(p)
		{
			res=g_string_new_len(p,len);
		}
		g_key_file_free(kf);
	}
	return res;
}

void set_signal(void)
{
	lxcom_set_signal_handler(SIGQUIT,lxdm_signal_handler,0);
	lxcom_set_signal_handler(SIGTERM,lxdm_signal_handler,0);
	lxcom_set_signal_handler(SIGINT,lxdm_signal_handler,0);
	lxcom_set_signal_handler(SIGHUP,lxdm_signal_handler,0);
	lxcom_set_signal_handler(SIGPIPE,lxdm_signal_handler,0);
	lxcom_set_signal_handler(SIGALRM,lxdm_signal_handler,0);
	signal(SIGTTIN,SIG_IGN);
	signal(SIGTTOU,SIG_IGN);
	signal(SIGUSR1,SIG_IGN);
	signal(SIGSEGV, sigsegv_handler);
}

int main(int arc, char *arg[])
{
	int daemonmode = 0;
	gboolean debugmode = FALSE;
	int i;


	for(i=1;i<arc;i++)
	{
		if(!strcmp(arg[i],"-d"))
		{
			daemonmode=1;
		}
		else if(!strcmp(arg[i],"-D"))
		{
			debugmode=TRUE;
		}
		else if(!strcmp(arg[i],"-c") && i+1<arc)
		{
			return lxcom_send("/var/run/lxdm/lxdm.sock",arg[i+1],NULL)?0:-1;			
		}
		else if(!strcmp(arg[i],"-w") && i+1<arc)
		{
			char *res=NULL;
			lxcom_send("/var/run/lxdm/lxdm.sock",arg[i+1],&res);
			if(res) printf("%s\n",res);
			g_free(res);
			return res?0:-1;
		}
	}
	if(getuid() != 0)
	{
		fprintf(stderr, "only root is allowed to use this program\n");
		exit(EXIT_FAILURE);
	}

	if(daemonmode)
	{
		(void)daemon(1, 1);
	}
	log_init();

	if(!debugmode)
	{
		/* turn off debug output */
		g_log_set_handler(NULL, G_LOG_LEVEL_DEBUG, log_ignore, NULL);
	}

	config = g_key_file_new();
	g_key_file_load_from_file(config, CONFIG_FILE, G_KEY_FILE_NONE, NULL);

	get_lock();
	
	if(0!=mkdir("/var/run/lxdm",0755))
	{
		if(errno==EEXIST)
		{
			chmod("/var/run/lxdm",0755);
		}
		else
		{
			g_critical("mkdir /var/run/lxdm fail\n");
			exit(-1);
		}
	}
	lxcom_init("/var/run/lxdm/lxdm.sock");
	atexit(exit_cb);

	set_signal();
	lxdm_get_tty();

	if(!auto_login_future())
		lxdm_do_auto_login();
	if(!lxsession_get_active())
		lxsession_greeter();

	lxcom_add_cmd_handler(-1,lxdm_user_cmd,NULL);

	ui_main();

	return 0;
}

