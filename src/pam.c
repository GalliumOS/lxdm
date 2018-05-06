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
#ifndef HAVE_LIBPAM
#ifdef USE_PAM
#define HAVE_LIBPAM 1
#else
#define HAVE_LIBPAM 0
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <pwd.h>
#include <grp.h>
#include <shadow.h>

#include <glib.h>

#include "lxdm.h"
#include "auth.h"

static void passwd_copy(struct passwd *dst,struct passwd *src)
{
	dst->pw_name=g_strdup(src->pw_name);
	dst->pw_uid=src->pw_uid;
	dst->pw_gid=src->pw_gid;
	if(src->pw_gecos)
		dst->pw_gecos=g_strdup(src->pw_gecos);
	dst->pw_dir=g_strdup(src->pw_dir);
	dst->pw_shell=g_strdup(src->pw_shell);
}

static void passwd_clean(struct passwd *pw)
{
	g_free(pw->pw_name);
	g_free(pw->pw_gecos);
	g_free(pw->pw_dir);
	g_free(pw->pw_shell);
	memset(pw,0,sizeof(*pw));
}

#if !HAVE_LIBPAM

int lxdm_auth_init(LXDM_AUTH *a)
{
	memset(a,0,sizeof(*a));
	return 0;
}

int lxdm_auth_cleanup(LXDM_AUTH *a)
{
	passwd_clean(&a->pw);
	return 0;
}

int lxdm_auth_user_authenticate(LXDM_AUTH *a,const char *user,const char *pass,int type)
{
	struct passwd *pw;
	struct spwd *sp;
	char *real;
	char *enc;
	if(!user || !user[0])
	{
		g_debug("user==NULL\n");
		return AUTH_ERROR;
	}
	pw = getpwnam(user);
	endpwent();
	if(!pw)
	{
		g_debug("user %s not found\n",user);
		return AUTH_BAD_USER;
	}
	if(strstr(pw->pw_shell, "nologin"))
	{
		g_debug("user %s have nologin shell\n",user);
		return AUTH_PRIV;
	}
	if(type==AUTH_TYPE_AUTO_LOGIN && !pass)
	{
		goto out;
	}
	sp = getspnam(user);
	if( !sp )
	{
		return AUTH_FAIL;
	}
	endspent();
	real = sp->sp_pwdp;
	if( !real || !real[0] )
	{
		if( !pass || !pass[0] )
		{
			passwd_copy(&a->pw,pw);
			g_debug("user %s auth with no password ok\n",user);
			return AUTH_SUCCESS;
		}
		else
		{
			g_debug("user %s password not match\n",user);
			return AUTH_FAIL;
		}
	}
	enc = crypt(pass, real);
	if( strcmp(real, enc) )
	{
		g_debug("user %s password not match\n",user);
		return AUTH_FAIL;
	}
out:
	g_debug("user %s auth ok\n",pw->pw_name);
	passwd_copy(&a->pw,pw);
	return AUTH_SUCCESS;
}

int lxdm_auth_session_begin(LXDM_AUTH *a,const char *name,int tty,int display,char mcookie[16])
{
	return 0;
}

int lxdm_auth_session_end(LXDM_AUTH *a)
{
	return 0;
}

int lxdm_auth_clean_for_child(LXDM_AUTH *a)
{
	return 0;
}

void lxdm_auth_print_env(LXDM_AUTH *a)
{
}

void lxdm_auth_put_env(LXDM_AUTH *a)
{
}

#else

#include <security/pam_appl.h>

static char *user_pass[2];

static int do_conv(int num, const struct pam_message **msg,struct pam_response **resp, void *arg)
{
	int result = PAM_SUCCESS;
	int i;
	*resp = (struct pam_response *) calloc(num, sizeof(struct pam_response));
	for(i=0;i<num;i++)
	{
		//printf("MSG: %d %s\n",msg[i]->msg_style,msg[i]->msg);
		switch(msg[i]->msg_style){
		case PAM_PROMPT_ECHO_ON:
			resp[i]->resp=strdup(user_pass[0]?user_pass[0]:"");
			break;
		case PAM_PROMPT_ECHO_OFF:
			//resp[i]->resp=strdup(user_pass[1]?user_pass[1]:"");
			resp[i]->resp=user_pass[1]?strdup(user_pass[1]):NULL;
			break;
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			//printf("PAM: %s\n",msg[i]->msg);
			break;
		default:
			break;
		}
	}
	return result;
}

static struct pam_conv conv={.conv=do_conv,.appdata_ptr=user_pass};

int lxdm_auth_init(LXDM_AUTH *a)
{
	memset(a,0,sizeof(*a));
	return 0;
}

int lxdm_auth_cleanup(LXDM_AUTH *a)
{
	passwd_clean(&a->pw);
	return 0;
}

int lxdm_auth_user_authenticate(LXDM_AUTH *a,const char *user,const char *pass,int type)
{
	struct passwd *pw;
	if(!user || !user[0])
	{
		g_debug("user==NULL\n");
		return AUTH_ERROR;
	}
	pw = getpwnam(user);
	endpwent();
	if(!pw)
	{
		g_debug("user %s not found\n",user);
		return AUTH_BAD_USER;
	}
	if(strstr(pw->pw_shell, "nologin"))
	{
		g_debug("user %s have nologin shell\n",user);
		return AUTH_PRIV;
	}
	if(a->handle) pam_end(a->handle,0);
	if(PAM_SUCCESS != pam_start("lxdm", pw->pw_name, &conv, (pam_handle_t**)&a->handle))
	{
		a->handle=NULL;
		g_debug("user %s start pam fail\n",user);
		return AUTH_FAIL;
	}
	else
	{
		int ret;
		if(type==AUTH_TYPE_AUTO_LOGIN && !pass)
			goto out;
		user_pass[0]=(char*)user;user_pass[1]=(char*)pass;
		ret=pam_authenticate(a->handle,PAM_SILENT);
		user_pass[0]=0;user_pass[1]=0;
		if(ret!=PAM_SUCCESS)
		{
			g_debug("user %s auth fail with %d\n",user,ret);
			return AUTH_FAIL;
		}
		ret=pam_acct_mgmt(a->handle,PAM_SILENT);
		if(ret!=PAM_SUCCESS)
		{
			g_debug("user %s acct mgmt fail with %d\n",user,ret);
			return AUTH_FAIL;
		}
	}
out:
	passwd_copy(&a->pw,pw);
	return AUTH_SUCCESS;
}

int lxdm_auth_session_begin(LXDM_AUTH *a,const char *name,int tty,int display,char mcookie[16])
{
	int err;
	char x[256];
	
	if(!a->handle)
	{
		return -1;
	}
	sprintf(x, "tty%d", tty);
	pam_set_item(a->handle, PAM_TTY, x);
#ifdef PAM_XDISPLAY
	sprintf(x,":%d",display);
	pam_set_item(a->handle, PAM_XDISPLAY, x);
#endif
#if !defined(DISABLE_XAUTH) && defined(PAM_XAUTHDATA)
	struct pam_xauth_data value;
	value.name="MIT-MAGIC-COOKIE-1";
	value.namelen=18;
	value.data=mcookie;
	value.datalen=16;
	pam_set_item (a->handle, PAM_XAUTHDATA, &value);
#endif
	if(name && name[0])
	{
		char *env;
		env = g_strdup_printf ("DESKTOP_SESSION=%s", name);
		pam_putenv (a->handle, env);
		g_free (env);
	}
	err = pam_open_session(a->handle, 0); /* FIXME pam session failed */
	if( err != PAM_SUCCESS )
	{
		g_warning( "pam open session error \"%s\"\n", pam_strerror(a->handle, err));
	}
	else
	{
		a->in_session=1;
	}
	return 0;
}

static int proc_filter(const struct dirent *d)
{
    int c=d->d_name[0];
    return c>='1' && c<='9';
}

static int check_process_sid(int pid,const char *sid)
{
	char path[128];
	FILE *fp;
	gchar *env_data,*p;
	gsize env_len;
	int res=0;

	sprintf(path,"/proc/%d/environ",pid);
	if(!g_file_get_contents(path,&env_data,&env_len,NULL))
	{
		return 0;
	}
	for(p=env_data;p!=NULL && p-env_data<env_len;)
	{
		if(!strncmp(p,"XDG_SESSION_ID=",15))
		{
			if(!strcmp(sid,p+15))
				res=1;
			break;
		}
		p=strchr(p,'\0');
		if(!p) break;p++;
	}
	g_free(env_data);

	return res;
}

static void kill_left_process(const char *sid)
{
	int self=getpid();
	struct dirent **list;
	int i,n;

	n=scandir("/proc",&list,proc_filter,0);
	if(n<0) return;
	for(i=0;i<n;i++)
	{
		int pid=atoi(list[i]->d_name);
		if(pid==self || pid<=1)
			continue;
		if(check_process_sid(pid,sid))
		{
			kill(pid,SIGKILL);
		}
	}
	free(list);
}

int lxdm_auth_session_end(LXDM_AUTH *a)
{
	int err;
	if(!a->handle)
		return 0;
	if(a->in_session)
	{
		char xdg_session_id[32]={0};
		const char *p=pam_getenv(a->handle,"XDG_SESSION_ID");
		if(p!=NULL) snprintf(xdg_session_id,32,"%s",p);
		err = pam_close_session(a->handle, 0);
		if( err != PAM_SUCCESS )
		{
			g_warning( "pam close session error \"%s\"\n", pam_strerror(a->handle, err));
		}
		a->in_session=0;
		if(p!=NULL)
		{
			usleep(100*1000);
			kill_left_process(xdg_session_id);
		}
	}
	pam_end(a->handle, err);
	a->handle = NULL;	
	passwd_clean(&a->pw);
	return 0;
}

int lxdm_auth_clean_for_child(LXDM_AUTH *a)
{
	pam_end(a->handle,0);
	return 0;
}

void lxdm_auth_print_env(LXDM_AUTH *a)
{
	int i;
	char **penv;
	if(!a->handle) return;
	penv=pam_getenvlist(a->handle);
	if(!penv) return;
	for(i=0;penv[i]!=NULL;i++)
	{
		if(i!=0) printf(" ");
		printf("%s",penv[i]);
	}
	free(penv);
}

void lxdm_auth_put_env(LXDM_AUTH *a)
{
	int i;
	char **penv;

	if(!a->handle) return;
	penv=pam_getenvlist(a->handle);
	if(!penv) return;
	for(i=0;penv[i]!=NULL;i++)
	{
		if(i!=0) printf(" ");
		if(0!=putenv(penv[i]))
			perror("putenv");
	}
	free(penv);
}

#endif

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
			setgid(pw->pw_gid) || setuid(pw->pw_uid) || setsid()==-1)
	{
		exit(EXIT_FAILURE);
	}
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
	execl("/etc/lxdm/Xsession","/etc/lxdm/Xsession",run,NULL);
	perror("execle");
	exit(EXIT_FAILURE);
}

void run_session(LXDM_AUTH *a,const char *run)
{
	a->child=fork();
	if(a->child==0)
	{
		lxdm_auth_put_env(a);
		lxdm_auth_clean_for_child(a);
		switch_user(&a->pw,run,NULL);
		//execle(run,run,NULL,environ);
		_exit(EXIT_FAILURE);
	}
}

LXDM_AUTH a;
static int session_exit=0;

static int xreadline(int fd,char *buf,size_t size)
{
	int i;
	for(i=0;i<size-1;i++)
	{
		int ret;
		do{
			ret=read(fd,buf+i,1);
		}while(ret==-1 && errno==EINTR);
		if(buf[i]==-1 || buf[i]=='\n')
			break;
	}
	buf[i]=0;
	return i;
}

int file_get_line(char *line, size_t n, FILE *fp)
{
	int len;
	
	if(session_exit)
		return -1;
/*	
	if(!fgets(line,n,fp))
		return -1;
	len=strcspn(line,"\r\n");
	line[len]=0;
*/

	struct pollfd fds;
	fds.fd=fileno(fp);
	fds.events=POLLIN;
	poll(&fds,1,-1);
	if(session_exit)
		return -1;

	len=xreadline(fileno(fp),line,n);
	return len;
}

void sig_handler(int sig)
{
	if(sig==SIGCHLD)
	{
		int wpid, status;
		while(1)
		{
			wpid = waitpid(-1,&status,0);
			if(wpid==a.child)
			{
				session_exit=1;
			}
			if(wpid<0) break;
		}
	}
}

int main(int arc,char *arg[])
{
	char cmd[128];
	int ret;

	setvbuf(stdout, NULL, _IOLBF, 0 );
	signal(SIGCHLD,sig_handler);

	lxdm_auth_init(&a);
	while(file_get_line(cmd,sizeof(cmd),stdin)>=0)
	{
		//fprintf(stderr,"begin %s\n",cmd);
		if(!strcmp(cmd,"auth"))
		{
			char temp[8],user[64],pass[64];
			int type;
			ret=file_get_line(temp,sizeof(temp),stdin);
			if(ret<0) break;
			type=atoi(temp);
			ret=file_get_line(user,sizeof(user),stdin);
			if(ret<0) break;
			if(type==AUTH_TYPE_NORMAL)
			{
				ret=file_get_line(pass,sizeof(pass),stdin);
				if(ret<0) break;
				ret=lxdm_auth_user_authenticate(&a,user,pass,type);
			}
			else
			{
				ret=lxdm_auth_user_authenticate(&a,user,NULL,type);
			}
			printf("%d\n",ret);
			if(ret==AUTH_SUCCESS)
			{
				printf("%d\n",a.pw.pw_uid);
				printf("%d\n",a.pw.pw_gid);
				printf("%s\n",a.pw.pw_gecos?:"");
				printf("%s\n",a.pw.pw_dir);
				printf("%s\n",a.pw.pw_shell);
			}
		}
		else if(!strcmp(cmd,"begin"))
		{
			char name[128],tty[8],display[8],mcookie[32];
			gsize out_len;
			ret=file_get_line(name,sizeof(name),stdin);
			if(ret<0) break;
			ret=file_get_line(tty,sizeof(tty),stdin);
			if(ret<0) break;
			ret=file_get_line(display,sizeof(display),stdin);
			if(ret<0) break;
			ret=file_get_line(mcookie,sizeof(mcookie),stdin);
			if(ret<0) break;
			g_base64_decode_inplace(mcookie,&out_len);
			ret=lxdm_auth_session_begin(&a,name,atoi(tty),atoi(display),mcookie);
			printf("%d\n",ret);
		}
		else if(!strcmp(cmd,"end"))
		{
			ret=lxdm_auth_session_end(&a);
			printf("%d\n",ret);
		}
		else if(!strcmp(cmd,"env"))
		{
			lxdm_auth_print_env(&a);
			printf("\n");
		}
		else if(!strcmp(cmd,"putenv"))
		{
			char env[1024];
			while(file_get_line(env,sizeof(env),stdin)>0)
			{
				putenv(strdup(env));
			}
		}
		else if(!strcmp(cmd,"exec"))
		{
			char run[256];
			if(file_get_line(run,sizeof(run),stdin)>0) {
				// some pam module likely replace the SIGCHLD handler
				signal(SIGCHLD,sig_handler);
				run_session(&a,run);
			}
		}
		else if(!strcmp(cmd,"exit"))
		{
			break;
		}
		//fprintf(stderr,"end\n");
	}
	lxdm_auth_session_end(&a);
	lxdm_auth_cleanup(&a);
	return 0;
}

