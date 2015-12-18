/*
 *      lxdm.h - interface of lxdm
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

#ifndef _LXDM_H_
#define _LXDM_H_

#include <glib.h>
#include <pwd.h>

G_BEGIN_DECLS

extern GKeyFile *config;

int lxdm_auth_user(int type,char *user,char *pass,struct passwd **ppw);
void lxdm_do_login(struct passwd *pw,char *session,char *lang,char *option);
void lxdm_do_reboot(void);
void lxdm_do_shutdown(void);
int lxdm_do_auto_login(void);
void lxdm_quit_self(int code);

enum AuthResult
{
    AUTH_SUCCESS,
    AUTH_BAD_USER,
    AUTH_FAIL,
    AUTH_PRIV,
    AUTH_ERROR
};

void ui_drop(void);
int ui_main(void);
void ui_prepare(void);
int ui_greeter_user(void);

typedef struct{
	char *name;
	char *exec;
	char* desktop_file;
}Session;

GSList *do_scan_xsessions(void);
void free_xsessions(GSList *);

G_END_DECLS

#endif/*_LXDM_H_*/

