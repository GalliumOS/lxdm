#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#if defined(__sun)
#include <ucred.h>
#include <sys/filio.h>
#elif !defined(linux) && !defined(__NetBSD__)
#include <sys/ucred.h>
#endif

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>

#ifndef SCM_CREDS
  #if defined(SCM_CREDENTIALS)
    #define SCM_CREDS SCM_CREDENTIALS
  #elif defined(SCM_UCRED)
    #define SCM_CREDS SCM_UCRED
  #else
    #error not support unix socket creds
  #endif
#endif

#ifndef linux
#  if defined(__sun)
#    define LXDM_PEER_UID(c)   ucred_geteuid(c)
#    define LXDM_PEER_GID(c)   ucred_getegid(c)
#    define LXDM_PEER_PID(c)   ucred_getpid(c)
#  elif !defined(__NetBSD__)
#    define LXDM_PEER_UID(c)   ((c)->cr_uid)
#    define LXDM_PEER_GID(c)   ((c)->cr_groups[0])
#    define LXDM_PEER_PID -1
#  else
#    define LXDM_PEER_UID(c)   ((c)->sc_uid)
#    define LXDM_PEER_GID(c)   ((c)->sc_gid)
#    define LXDM_PEER_PID -1
#  endif
#else
#  define LXDM_PEER_UID(c)   ((c)->uid)
#  define LXDM_PEER_GID(c)   ((c)->gid)
#  define LXDM_PEER_PID(c)   ((c)->pid)
#endif

#if defined(__NetBSD__)
typedef struct sockcred LXDM_CRED;
#elif defined(__sun)
typedef ucred_t LXDM_CRED;
#else
typedef struct ucred LXDM_CRED;
#endif

#include <glib.h>
#include "lxcom.h"

static const char *sock_path;
static int self_client_fd=-1;
static int self_server_fd=-1;
static int self_source_id=0;

typedef struct{
	void *data;
	int pid;
	void (*func)(void *data,int pid,int status);
}ChildWatch;

typedef struct{
	int signal;
	void *data;
	void (*func)(void *data,int signal);
}SignalHandler;

typedef struct{
	int user;
	void *data;
	GString *(*func)(void *data,int user,int argc,char **argv);
}UserCmd;

static GSList *child_watch_list;
static GSList *signal_handler_list;
static GSList *user_cmd_list;

typedef struct _LXComSource
{
	GSource source;
	GPollFD poll;
}LXComSource;

typedef GString *(*LXComFunc)(gpointer data,int uid,int pid,int argc,char **argv);

static gboolean lxcom_prepare (GSource *source,gint *timeout)
{
	*timeout = -1;
	return FALSE;
}

static gboolean lxcom_check(GSource *source)
{
	return (((LXComSource*)source)->poll.revents&G_IO_IN)?TRUE:FALSE;
}

static gboolean lxcom_dispatch (GSource *source,GSourceFunc callback,gpointer user_data)
{
	char buf[4096];
	char ctrl[/*CMSG_SPACE(sizeof(LXDM_CRED))*/1024];
	struct sockaddr_un peer;
	struct iovec v={buf,sizeof(buf)};
	struct msghdr h={&peer,sizeof(peer),&v,1,ctrl,sizeof(ctrl),0};
	struct cmsghdr *cmptr;
	int ret;

	while(1)
	{
		peer.sun_family=0;
		ret=recvmsg(self_server_fd,&h,0);

		if(ret<0) break;
		if(ret>4000) continue;
		buf[ret]=0;
		for(cmptr = CMSG_FIRSTHDR(&h);cmptr!=NULL;cmptr=CMSG_NXTHDR(&h,cmptr))
		{
			LXDM_CRED *c;
			int size;
			int argc;
			char **argv;
			GString *res;

			#if defined(__sun)
			size = ucred_size();
			#elif defined(__NetBSD__)
			if (cmptr->cmsg_len < SOCKCREDSIZE(0)) break;
			size = SOCKCREDSIZE(((cred *)CMSG_DATA(cmptr))->sc_ngroups);
			#else
			size = sizeof(LXDM_CRED);
			#endif
			if (cmptr->cmsg_len != CMSG_LEN(size)) break;
			if (cmptr->cmsg_level != SOL_SOCKET) break;
			if (cmptr->cmsg_type != SCM_CREDS) break;
			c=(LXDM_CRED*)CMSG_DATA(cmptr);
			if(g_shell_parse_argv(buf,&argc,&argv,NULL))
			{
				res=((LXComFunc)callback)(user_data,LXDM_PEER_UID(c),LXDM_PEER_PID(c),argc,argv);
				g_strfreev(argv);
				if(res)
				{
					do{
						ret=sendto(self_server_fd,res->str,res->len,0,(struct sockaddr*)&peer,sizeof(peer));
					}while(ret==-1 && errno==EINTR);
					g_string_free(res,TRUE);
					if(ret==-1) perror("sendto");
				}
			}
			break;
		}
	}

	return TRUE;
}

static GSourceFuncs lxcom_funcs =
{
 	lxcom_prepare,
	lxcom_check,
	lxcom_dispatch,
	NULL
};

static GString *lxcom_func(gpointer data,int uid,int pid,int argc,char **argv)
{
	gboolean deal=FALSE;
	GSList *p,*n;
	GString *res=NULL;
	assert(argc>0 && argv!=NULL);
	do{
	if(!strcmp(argv[0],"SIGNAL"))
	{
		if(argc!=2) break;
		if((pid==-1 && uid==0) || pid==getpid())
		{
			int sig=atoi(argv[1]);
			if(sig==SIGCHLD)
			{
				CHECK_SIGCHLD:
				for(p=child_watch_list;p!=NULL;p=p->next)
				{
					ChildWatch *item=p->data;
					int status;
					if(waitpid(item->pid,&status,WNOHANG)>0)
					{
						child_watch_list=g_slist_delete_link(child_watch_list,p);
						// item->func may change the child_watch_list
						item->func(item->data,item->pid,status);
						g_free(item);
						goto CHECK_SIGCHLD;
					}
				}
			}
			else
			{
				for(p=signal_handler_list;p!=NULL;p=p->next)
				{
					SignalHandler *item=p->data;
					if(item->signal==sig)
					{
						item->func(item->data,sig);
						break;
					}
				}
			}
		}
		deal=TRUE;
		break;
	}
	if(!deal) for(p=user_cmd_list;p!=NULL;p=n)
	{
		UserCmd *item=p->data;
		n=p->next;
		if(item->user==uid)
		{
			res=item->func(item->data,uid,argc,argv);
			deal=TRUE;
			break;
		}
	}
	if(!deal) for(p=user_cmd_list;p!=NULL;p=n)
	{
		UserCmd *item=p->data;
		n=p->next;
		if(item->user==-1)
		{
			res=item->func(item->data,uid,argc,argv);
			deal=TRUE;
			break;
		}
	}
	}while(0);	
	return res;
}

volatile sig_atomic_t lxcom_last_sig;
static void sig_handler(int sig)
{
	lxcom_last_sig=sig;
	lxcom_raise_signal(sig);
}

static void lxcom_exit_cb(void)
{
	if(sock_path)
		unlink(sock_path);
}

void lxcom_init(const char *sock)
{
	struct sockaddr_un su;
	int ret,on=1;
	struct sigaction action;
	struct stat st;

	sock_path=sock;
	atexit(lxcom_exit_cb);

	LXComSource *s=(LXComSource*)g_source_new(&lxcom_funcs,sizeof(LXComSource));
	g_source_set_callback((GSource*)s,(GSourceFunc)lxcom_func,NULL,NULL);

	unlink(sock);
	memset(&su,0,sizeof(su));
	su.sun_family=AF_UNIX;
	strcpy(su.sun_path,sock);
	self_server_fd=socket(AF_UNIX,SOCK_DGRAM,0);
	assert(self_server_fd!=-1);
#if defined(__sun)
	ret=setsockopt(self_server_fd,SOL_SOCKET,SO_RECVUCRED,&on,sizeof(on));
#else
	ret=setsockopt(self_server_fd,SOL_SOCKET,SO_PASSCRED,&on,sizeof(on));
#endif
	assert(ret==0);
	fcntl(self_server_fd,F_SETFL,O_NONBLOCK|fcntl(self_server_fd,F_GETFL));
	ret=bind(self_server_fd,(struct sockaddr*)&su,sizeof(su));
	assert(ret==0);
	self_client_fd=socket(AF_UNIX,SOCK_DGRAM,0);
	assert(self_client_fd!=-1);
	fcntl(self_client_fd,F_SETFL,O_NONBLOCK|fcntl(self_client_fd,F_GETFL));
	ret=connect(self_client_fd,(struct sockaddr*)&su,sizeof(su));
	assert(ret==0);

	s->poll.fd=self_server_fd;
	s->poll.events=G_IO_IN;
	s->poll.revents=0;
	g_source_add_poll((GSource*)s,&s->poll);	
	self_source_id=g_source_attach((GSource*)s,NULL);

	action.sa_handler = sig_handler;
	sigemptyset (&action.sa_mask);
	action.sa_flags = SA_NOCLDSTOP;
	sigaction (SIGCHLD, &action, NULL);

	if(!stat(sock,&st))
	{
		chmod(sock,st.st_mode|S_IWOTH|S_IWGRP);
	}
}

static ssize_t lxcom_write(int s,const void *buf,size_t count)
{
	struct iovec iov[1] ={{(void*)buf,count,}};
	struct msghdr msg = { 0, 0, iov, 1, 0, 0, 0 };
#if !defined(linux) && !defined(__NetBSD__)

#if defined(__sun)
	int size = ucred_size();
#else
	int size = sizeof(LXDM_CRED);
#endif
	char ctrl[CMSG_SPACE(size)];
	struct cmsghdr  *cmptr;
	char *p;
	int i;

	msg.msg_control    = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	cmptr = CMSG_FIRSTHDR(&msg);
	cmptr->cmsg_len   = CMSG_LEN(size);
	cmptr->cmsg_level = SOL_SOCKET;
	cmptr->cmsg_type  = SCM_CREDS;
	p=(char*)CMSG_DATA(cmptr);
	for(i=0;i<size;i++)
		p[i]=0;
#endif
	return sendmsg(s,&msg,0);
}

void lxcom_raise_signal(int sig)
{
	char temp[32]="SIGNAL ";
	int pos=7,val;
	val=(sig/10)%10;if(val) temp[pos++]='0'+val;
	val=sig%10;temp[pos++]='0'+val;
	temp[pos]=0;
	lxcom_write(self_client_fd,temp,sizeof(temp));
}

gboolean lxcom_send(const char *sock,const char *buf,char **res)
{
	int s;
	int ret;
	struct sockaddr_un su;
	int count=strlen(buf)+1;
	char *addr=NULL;
	
	memset(&su,0,sizeof(su));
	su.sun_family=AF_UNIX;
	s=socket(AF_UNIX,SOCK_DGRAM,0);
	assert(s!=-1);
	fcntl(s,F_SETFL,O_NONBLOCK|fcntl(self_client_fd,F_GETFL));
	strcpy(su.sun_path,sock);
	ret=connect(s,(struct sockaddr*)&su,sizeof(su));
	if(ret!=0)
	{
		perror("connect");
		close(s);
		return -1;
	}

	if(res)
	{
#ifdef __linux__
		su.sun_path[0]=0;
		sprintf(su.sun_path+1,"/var/run/lxdm/lxdm-%d.sock",getpid());
#else
		addr=g_strdup_printf("/var/run/lxdm/lxdm-%d.sock",getpid());
		unlink(addr);
		strcpy(su.sun_path,addr);
#endif
		ret=bind(s,(struct sockaddr*)&su,sizeof(su));
		if(ret!=0)
		{
			close(s);
			g_free(addr);
			perror("bind");
			return FALSE;
		}
	}

	ret=lxcom_write(s,buf,count);
	if(ret!=count)
	{
		close(s);
		if(addr) unlink(addr);
		g_free(addr);
		return FALSE;
	}
	if(res)
	{
		struct pollfd pf;
		*res=NULL;
		pf.fd=s;
		pf.events=POLLIN;
		pf.revents=0;
		ret=poll(&pf,1,3000);
		if(ret==1 && (pf.revents & POLLIN))
		{
			ret=ioctl(s,FIONREAD,&count);
			if(ret==0)
			{
				char *p=g_malloc(count+1);
				ret=recv(s,p,count,0);
				if(ret>=0)
				{
					p[ret]=0;
					*res=p;
				}
				else
				{
					g_free(p);
				}
			}
		}
	}
	close(s);
	if(addr) unlink(addr);
	g_free(addr);
	return TRUE;
}

int lxcom_add_child_watch(int pid,void (*func)(void*,int,int),void *data)
{
	ChildWatch *item;
	if(pid<=0 || !func)
		return -1;
	item=g_new(ChildWatch,1);
	item->func=func;
	item->data=data;
	item->pid=pid;
	child_watch_list=g_slist_prepend(child_watch_list,item);
	return 0;
}

int lxcom_del_child_watch(int pid)
{
	GSList *p;
	for(p=child_watch_list;p!=NULL;p=p->next)
	{
		ChildWatch *item=p->data;
		if(item->pid==pid)
		{
			child_watch_list=g_slist_delete_link(child_watch_list,p);
			g_free(item);
			return 0;
		}
	}
	return -1;
}

int lxcom_set_signal_handler(int sig,void (*func)(void *,int),void *data)
{
	SignalHandler *item;
	struct sigaction action;
	if(sig<=0 || sig>=64 || !func)
		return -1;
	if(sig==SIGCHLD)
		return -1;

	item=g_new(SignalHandler,1);
	item->data=data;
	item->signal=sig;
	item->func=func;
	signal_handler_list=g_slist_prepend(signal_handler_list,item);

	action.sa_handler = sig_handler;
	sigemptyset (&action.sa_mask);
	action.sa_flags = 0;
	sigaction (sig, &action, NULL);

	return 0;
}

int lxcom_add_cmd_handler(int user,GString *(*func)(void *,int,int,char **),void *data)
{
	UserCmd *item;
	if(!func)
		return -1;
	item=g_new(UserCmd,1);
	item->data=data;
	item->user=user;
	item->func=func;
	user_cmd_list=g_slist_prepend(user_cmd_list,item);
	return 0;
}

int lxcom_del_cmd_handler(int user)
{
	GSList *p;
	for(p=user_cmd_list;p!=NULL;p=p->next)
	{
		UserCmd *item=p->data;
		if(item->user==user)
		{
			user_cmd_list=g_slist_remove_link(user_cmd_list,p);
			g_free(item);
			return 0;
		}
	}
	return -1;
}

#if 0

#include <poll.h>

void poll_sock(void)
{
	struct pollfd pf;
	pf.fd=self_server_fd;
	pf.events=POLLIN;
	pf.revents=0;
	poll(&pf,1,-1);
	printf("%x\n",pf.revents);
}

void on_sigint(void *data,int sig)
{
	printf("signal SIGINT %s\n",(char*)data);
	exit(0);
}

int cmd_count;
GString *on_cmd(void *data,int user,int arc,char **arg)
{
	int i;
	printf("%d %s: ",user,(char*)data);
	for(i=0;i<arc;i++)
	{
		printf("%s ",arg[i]);
	}
	printf("\n");
	cmd_count++;

	if(cmd_count==2)
		lxcom_del_cmd_handler(user);
	return g_string_new("OK");
}

int main(int arc,char *arg[])
{
	if(arc==1)
	{
		GMainLoop *loop;
		lxcom_init("/tmp/test.sock");
		lxcom_set_signal_handler(SIGINT,on_sigint,"sigtest");
		lxcom_add_cmd_handler(-1,on_cmd,"cmdtest");
		loop=g_main_loop_new(NULL,0);
		g_main_loop_run(loop);
	}
	else
	{
		gboolean ret;
		char *res=0;
		ret=lxcom_send("/tmp/test.sock",arg[1],&res);
		if(ret && res) printf("%s\n",res);
	}
	return 0;
}
#endif

