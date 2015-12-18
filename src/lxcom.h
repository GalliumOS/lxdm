#ifndef _LXCOM_H_
#define LXCOM_H_

void lxcom_init(const char *sock);
void lxcom_raise_signal(int sig);
gboolean lxcom_send(const char *sock,const char *buf,char **res);
int lxcom_add_child_watch(int pid,void (*func)(void*,int,int),void *data);
int lxcom_del_child_watch(int pid);
int lxcom_set_signal_handler(int sig,void (*func)(void *,int),void *data);
int lxcom_add_cmd_handler(int user,GString * (*func)(void *,int,int,char **),void *data);
int lxcom_del_cmd_handler(int user);

extern volatile sig_atomic_t lxcom_last_sig;

#endif/*_LXCOM_H_*/

