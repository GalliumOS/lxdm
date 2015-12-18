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

#include <pwd.h>
#include <grp.h>
#include <shadow.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>

#include "lxdm.h"
#include "auth.h"

#if HAVE_LIBPAM

#define PAM_MP	1

#endif

void switch_user(struct passwd *pw, const char *run, char **env);

static void passwd_clean(struct passwd *pw)
{
	g_free(pw->pw_name);
	g_free(pw->pw_gecos);
	g_free(pw->pw_dir);
	g_free(pw->pw_shell);
	memset(pw,0,sizeof(*pw));
}

#if !PAM_MP
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
#endif

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

char **lxdm_auth_append_env(LXDM_AUTH *a,char **env)
{
	return env;
}

int lxdm_auth_session_run(LXDM_AUTH *a,const char *session_exec,char **env)
{
	int pid;
	pid = fork();
	if(pid==0)
	{
		env=lxdm_auth_append_env(a,env);
		lxdm_auth_clean_for_child(a);
		switch_user(&a->pw, session_exec, env);
		lxdm_quit_self(4);
	}
	return pid;
}

#elif !PAM_MP

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
		g_message("begin session without auth\n");
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
		g_warning( "pam open session error \"%s\"\n", pam_strerror(a->handle, err));
	else
		a->in_session=1;
	return 0;
}

int lxdm_auth_session_end(LXDM_AUTH *a)
{
	int err;
	if(!a->handle)
		return 0;
	if(a->in_session)
	{
		err = pam_close_session(a->handle, 0);
		a->in_session=0;
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

char **lxdm_auth_append_env(LXDM_AUTH *a,char **env)
{
	int i,j,n,pa;
	char **penv;
	if(!a->handle) return env;
	penv=pam_getenvlist(a->handle);
	if(!penv) return env;
	pa=g_strv_length(penv);
	if(pa==0)
	{
		free(penv);
		return env;
	}
	env=g_renew(char *,env,g_strv_length(env)+1+pa+10);
	for(i=0;penv[i]!=NULL;i++)
	{
		fprintf(stderr,"PAM %s\n",penv[i]);
		n=strcspn(penv[i],"=")+1;
		for(j=0;env[j]!=NULL;j++)
		{
			if(!strncmp(penv[i],env[j],n))
				break;
			if(env[j+1]==NULL)
			{
				env[j+1]=g_strdup(penv[i]);
				env[j+2]=NULL;
				break;
			}
		}
		free(penv[i]);
	}
	free(penv);
	return env;
}

int lxdm_auth_session_run(LXDM_AUTH *a,const char *session_exec,char **env)
{
	int pid;
	pid = fork();
	if(pid==0)
	{
		env=lxdm_auth_append_env(a,env);
		lxdm_auth_clean_for_child(a);
		switch_user(&a->pw, session_exec, env);
		lxdm_quit_self(4);
	}
	return pid;
}

#else

static void xwrite(int fd,const void *buf,size_t size)
{
	int ret;
	do{
		ret=write(fd,buf,size);
	}while(ret==-1 && errno==EINTR);
}

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

int lxdm_auth_init(LXDM_AUTH *a)
{
	memset(a,0,sizeof(*a));
	a->pipe[0]=a->pipe[1]=-1;
	return 0;
}

int lxdm_auth_cleanup(LXDM_AUTH *a)
{
	passwd_clean(&a->pw);
	if(a->pipe[0]!=-1)
	{
		close(a->pipe[0]);
		a->pipe[0]=-1;
	}
	if(a->pipe[1]!=-1)
	{
		close(a->pipe[1]);
		a->pipe[1]=-1;
	}
	return 0;
}

//#undef LXDM_SESSION_PATH
//#define LXDM_SESSION_PATH "./lxdm-session"
static int check_child(LXDM_AUTH *a)
{
	if(a->pipe[0]!=-1)
		return 0;
	char *argv[3]={LXDM_SESSION_PATH,NULL,NULL};
	GPid pid;
	gboolean ret;
	ret = g_spawn_async_with_pipes(NULL, argv, NULL,
				   G_SPAWN_DO_NOT_REAP_CHILD, NULL,NULL,
				   &pid, a->pipe + 0, a->pipe + 1, NULL, NULL);
	if(ret==FALSE)
	{
		g_message("spawn lxdm-session fail\n");
		return -1;
	}
	a->child=(int)pid;
	return 0;
}

int lxdm_auth_user_authenticate(LXDM_AUTH *a,const char *user,const char *pass,int type)
{
	char temp[128];
	char res[8];
	int ret;
	if(check_child(a)!=0)
	{
		printf("check child fail\n");
		return -1;
	}
	if(type==AUTH_TYPE_AUTO_LOGIN && pass)
		type=AUTH_TYPE_NORMAL;
	else if(type==AUTH_TYPE_NORMAL && !pass)
		type=AUTH_TYPE_NULL_PASS;
	xwrite(a->pipe[0],"auth\n",5);
	ret=sprintf(temp,"%d\n",type);
	xwrite(a->pipe[0],temp,ret);
	ret=sprintf(temp,"%s\n",user);
	xwrite(a->pipe[0],temp,ret);
	if(pass!=NULL)
		ret=sprintf(temp,"%s\n",pass);
	xwrite(a->pipe[0],temp,ret);
	ret=xreadline(a->pipe[1],res,sizeof(res));
	if(ret<=0)
	{
		g_message("read user auth result fail\n");
		return -1;
	}
	ret=atoi(res);
	if(ret==AUTH_SUCCESS)
	{
		passwd_clean(&a->pw);
		a->pw.pw_name=g_strdup(user);
		ret=xreadline(a->pipe[1],temp,sizeof(temp));
		if(ret==-1) return -1;
		a->pw.pw_uid=atoi(temp);
		ret=xreadline(a->pipe[1],temp,sizeof(temp));
		if(ret==-1) return -1;
		a->pw.pw_gid=atoi(temp);
		ret=xreadline(a->pipe[1],temp,sizeof(temp));
		if(ret==-1) return -1;
		a->pw.pw_gecos=g_strdup(temp);
		ret=xreadline(a->pipe[1],temp,sizeof(temp));
		if(ret==-1) return -1;
		a->pw.pw_dir=g_strdup(temp);
		ret=xreadline(a->pipe[1],temp,sizeof(temp));
		if(ret==-1) return -1;
		a->pw.pw_shell=g_strdup(temp);
	}
	return atoi(res);
}
#include <assert.h>
int lxdm_auth_session_begin(LXDM_AUTH *a,const char *name,int tty,int display,char mcookie[16])
{
	char temp[256];
	char res[8];
	gchar *b64;
	int ret;

	if(check_child(a)!=0)
		return -1;
	xwrite(a->pipe[0],"begin\n",6);
	ret=sprintf(temp,"%s\n",name?:"");
	xwrite(a->pipe[0],temp,ret);
	ret=sprintf(temp,"%d\n",tty);
	xwrite(a->pipe[0],temp,ret);
	ret=sprintf(temp,"%d\n",display);
	xwrite(a->pipe[0],temp,ret);
	b64=g_base64_encode((const guchar*)mcookie,16);
	assert(b64!=NULL);
	ret=sprintf(temp,"%s\n",b64);
	g_free(b64);
	xwrite(a->pipe[0],temp,ret);
	ret=xreadline(a->pipe[1],res,sizeof(res));
	if(ret<=0)
	{
		g_message("pam session begin fail\n");
		return -1;
	}
	ret=atoi(res);
	return ret;
}

int lxdm_auth_session_end(LXDM_AUTH *a)
{
	passwd_clean(&a->pw);
	if(a->pipe[0]!=-1)
	{
		xwrite(a->pipe[0],"exit\n",5);
		close(a->pipe[0]);
		a->pipe[0]=-1;
	}
	if(a->pipe[1]!=-1)
	{
		close(a->pipe[1]);
		a->pipe[1]=-1;
	}
	return 0;
}

int lxdm_auth_clean_for_child(LXDM_AUTH *a)
{
	return 0;
}

char **lxdm_auth_append_env(LXDM_AUTH *a,char **env)
{
	int i,j,n,pa;
	char temp[1024];
	int ret;
	char **penv;
	
	if(check_child(a)!=0)
		return env;
	xwrite(a->pipe[0],"env\n",4);
	ret=xreadline(a->pipe[1],temp,sizeof(temp));
	if(ret<=0) return env;
	penv=g_strsplit(temp," ",-1);
	pa=g_strv_length(penv);
	if(pa==0)
	{
		g_strfreev(penv);
		return env;
	}
	env=g_renew(char *,env,g_strv_length(env)+1+pa+10);
	for(i=0;penv[i]!=NULL;i++)
	{
		g_debug("PAM %s\n",penv[i]);
		n=strcspn(penv[i],"=")+1;
		for(j=0;env[j]!=NULL;j++)
		{
			if(!strncmp(penv[i],env[j],n))
				break;
			if(env[j+1]==NULL)
			{
				env[j+1]=g_strdup(penv[i]);
				env[j+2]=NULL;
				break;
			}
		}
	}
	g_strfreev(penv);
	return env;
}

int lxdm_auth_session_run(LXDM_AUTH *a,const char *session_exec,char **env)
{
	int fd;
	if(check_child(a)!=0)
		return -1;
	fd=a->pipe[0];
	if(env!=NULL)
	{
		int i;
		xwrite(fd,"putenv\n",7);
		for(i=0;env[i]!=NULL;i++)
		{
			xwrite(fd,env[i],strlen(env[i]));
			xwrite(fd,"\n",1);
		}
		xwrite(a->pipe[0],"\n",1);
	}
	xwrite(fd,"exec\n",5);
	xwrite(fd,session_exec,strlen(session_exec));
	xwrite(fd,"\n",1);
	return a->child;
}

#endif

