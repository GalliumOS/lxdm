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

#pragma once

typedef struct{
	void *handle;
	struct passwd pw;
	int pipe[2];
	int child;
	int in_session;
}LXDM_AUTH;

enum{
	AUTH_TYPE_NORMAL=0,
	AUTH_TYPE_AUTO_LOGIN,
	AUTH_TYPE_NULL_PASS
};

int lxdm_auth_init(LXDM_AUTH *a);
int lxdm_auth_cleanup(LXDM_AUTH *a);
int lxdm_auth_user_authenticate(LXDM_AUTH *a,const char *user,const char *pass,int type);
int lxdm_auth_session_begin(LXDM_AUTH *a,const char *name,int tty,int display,char mcookie[16]);
int lxdm_auth_session_end(LXDM_AUTH *a);
int lxdm_auth_clean_for_child(LXDM_AUTH *a);
char **lxdm_auth_append_env(LXDM_AUTH *a,char **env);
int lxdm_auth_session_run(LXDM_AUTH *a,const char *session_exec,char **env);
