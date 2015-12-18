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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>

#define CONFIG_UI_FILE		LXDM_DATA_DIR"/config.ui"
//#define CONFIG_UI_FILE		"../data/config.ui"
#define FACES_DIR			"/usr/share/pixmaps/faces"

#define ROW_SPAN 6

GtkBuilder *builder;
GKeyFile *config;
int dirty;
char *user_name;
struct passwd *user;
gboolean root;
char *theme_name;
char *theme_dir;
char *ui_nobody;
GtkWidget *photo_popup;

void prepare_user(void)
{
	root=getuid()==0;
	if(!user_name)
	{
		if(!root)
			user_name=getenv("USER");
		if(!user_name)
			user_name=getenv("SUDO_USER");
		if(!user_name)
			user_name=getenv("USER");
		if(!user_name)
			exit(1);
	}
	user=getpwnam(user_name);
	if(!user)
	{
		exit(1);
	}
}

void prepare_config(void)
{
	config=g_key_file_new();
	g_key_file_load_from_file(config,CONFIG_FILE,G_KEY_FILE_KEEP_COMMENTS,NULL);
	theme_name=g_key_file_get_string(config,"display", "theme", NULL);
	theme_dir=g_build_filename(LXDM_DATA_DIR "/themes", theme_name, NULL);
	
	ui_nobody = g_build_filename(theme_dir, "nobody.png", NULL);
    if( !g_file_test(ui_nobody, G_FILE_TEST_EXISTS) )
    {
        g_free(ui_nobody);
        ui_nobody = NULL;
    }
}

void popup_menu_below_button (GtkMenu   *menu,
                         gint      *x,
                         gint      *y,
                         gboolean  *push_in,
                         GtkWidget *button)
{
	GtkRequisition menu_req;
	GtkTextDirection direction;
	GtkAllocation allocation;

#if GTK_CHECK_VERSION(3,0,0)
	gtk_widget_get_preferred_size (GTK_WIDGET (menu), NULL, &menu_req);
#else
	gtk_widget_size_request(GTK_WIDGET(menu),&menu_req);
#endif

	direction = gtk_widget_get_direction (button);

	gdk_window_get_origin (gtk_widget_get_window (button), x, y);
	gtk_widget_get_allocation (button, &allocation);
	*x += allocation.x;
	*y += allocation.y + allocation.height;

	if (direction == GTK_TEXT_DIR_LTR)
		*x += MAX (allocation.width - menu_req.width, 0);
	else if (menu_req.width > allocation.width)
		*x -= menu_req.width - allocation.width;

	*push_in = TRUE;
}

static gboolean image_file_valid(const char *filename)
{
	GdkPixbuf *pixbuf;
	pixbuf=gdk_pixbuf_new_from_file(filename,NULL);
	if(pixbuf) g_object_unref(pixbuf);
	return pixbuf?TRUE:FALSE;
}

static void update_face_image(GtkWidget *w)
{
	GdkPixbuf *pixbuf;
	char *path=g_build_filename(user->pw_dir,".face",NULL);
	pixbuf=gdk_pixbuf_new_from_file_at_scale(path,48,48,FALSE,NULL);
	g_free(path);
	if(!pixbuf && ui_nobody)
		pixbuf=gdk_pixbuf_new_from_file_at_scale(ui_nobody,48,48,FALSE,NULL);
	if(!pixbuf)
		pixbuf=gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
						"avatar-default", 48,GTK_ICON_LOOKUP_FORCE_SIZE,NULL);
	if(pixbuf)
	{
		gtk_image_set_from_pixbuf(GTK_IMAGE(w),pixbuf);
		g_object_unref(pixbuf);
	}
}

static void set_face_file(const char *filename)
{
	GtkWidget *w;
	if(filename)
	{
		gchar *contents;
		gsize length;
		GdkPixbuf *pixbuf;
		pixbuf=gdk_pixbuf_new_from_file(filename,NULL);
		if(!pixbuf) return;
		g_object_unref(pixbuf);
		if(g_file_get_contents(filename,&contents,&length,NULL))
		{
			gchar *path=g_build_filename(user->pw_dir,".face",NULL);
			g_file_set_contents(path,contents,length,NULL);
			chown(path,user->pw_uid,user->pw_gid);
			g_free(path);
		}
	}
	else
	{
		gchar *path=g_build_filename(user->pw_dir,".face",NULL);
		unlink(path);
		g_free(path);
	}
	w=(GtkWidget*)gtk_builder_get_object(builder,"user-icon-image");
	update_face_image(w);
}

static void stock_icon_selected (GtkMenuItem *menuitem)
{
	const char *filename;
	filename = g_object_get_data (G_OBJECT (menuitem), "filename");
    set_face_file(filename);
}

static GtkWidget *menu_item_for_filename(const char *filename)
{
	GtkWidget *image, *menuitem;
	GFile *file;
	GIcon *icon;

	file = g_file_new_for_path (filename);
	icon = g_file_icon_new (file);
	g_object_unref (file);
	image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DIALOG);
	g_object_unref (icon);

	menuitem = gtk_menu_item_new ();
	gtk_container_add (GTK_CONTAINER (menuitem), image);
	gtk_widget_show_all (menuitem);

	g_object_set_data_full (G_OBJECT (menuitem), "filename",
							g_strdup (filename), (GDestroyNotify) g_free);
	g_signal_connect (G_OBJECT (menuitem), "activate",
					  G_CALLBACK (stock_icon_selected), NULL);

	return menuitem;
}

static void
on_photo_popup_unmap (GtkWidget *popup_menu,
                      GtkWidget *popup_button)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (popup_button), FALSE);
}

static void
update_preview (GtkFileChooser *chooser)
{
	gchar *uri;

	uri = gtk_file_chooser_get_preview_filename(chooser);

	if (uri)
	{
		GdkPixbuf *pixbuf = NULL;
		GtkWidget *preview;

		preview = gtk_file_chooser_get_preview_widget (chooser);

		pixbuf=gdk_pixbuf_new_from_file_at_scale(uri,96,96,TRUE,NULL);

		gtk_dialog_set_response_sensitive (GTK_DIALOG (chooser),
										   GTK_RESPONSE_ACCEPT,
										   (pixbuf != NULL));

		if (pixbuf != NULL) {
				gtk_image_set_from_pixbuf (GTK_IMAGE (preview), pixbuf);
				g_object_unref (pixbuf);
		}
		else {
				gtk_image_set_from_stock (GTK_IMAGE (preview),
										  GTK_STOCK_DIALOG_QUESTION,
										  GTK_ICON_SIZE_DIALOG);
		}

		g_free (uri);
	}

	gtk_file_chooser_set_preview_widget_active (chooser, TRUE);
}

static void file_icon_selected (GtkMenuItem *menuitem)
{
	gint res;
	GtkWidget *w;
	const gchar *folder;
	folder = g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-image-chooser");
	if(folder)
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w),folder);
	g_signal_connect(w,"update-preview",G_CALLBACK(update_preview),NULL);
	res=gtk_dialog_run(GTK_DIALOG(w));
	if(res==GTK_RESPONSE_ACCEPT)
	{
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w));
		set_face_file(filename);
		g_free(filename);
	}
	gtk_widget_hide(GTK_WIDGET(w));
}

void on_user_icon_toggled(GtkToggleButton *togglebutton,gpointer user_data)
{
	if(!photo_popup)
	{
		gboolean added_faces=TRUE;
		GtkWidget *menu,*menuitem;
		int x=0,y=0;
		GDir *dir;
		const char *face;
		menu=gtk_menu_new();
		
		dir=g_dir_open(FACES_DIR,0,NULL);
		if(dir) while((face=g_dir_read_name(dir))!=NULL)
		{
			char *path=g_build_filename(FACES_DIR,face,NULL);
			menuitem=menu_item_for_filename(path);
			g_free(path);
			gtk_menu_attach (GTK_MENU (menu), GTK_WIDGET (menuitem),
						x, x + 1, y, y + 1);
			gtk_widget_show (menuitem);
			x++;
			if (x >= ROW_SPAN - 1)
			{
				y++;
				x = 0;
			}
			added_faces = TRUE;
		}
		if(added_faces)
		{
			GtkWidget *image = gtk_image_new_from_icon_name("avatar-default", GTK_ICON_SIZE_DIALOG);
			menuitem = gtk_menu_item_new ();
			gtk_container_add (GTK_CONTAINER (menuitem), image);
			gtk_widget_show_all (menuitem);
			gtk_menu_attach (GTK_MENU (menu), GTK_WIDGET (menuitem),
							 x, x + 1, y, y + 1);
			g_signal_connect (G_OBJECT (menuitem), "activate",
							  G_CALLBACK (stock_icon_selected), NULL);
			gtk_widget_show (menuitem);
			y++;

			menuitem = gtk_separator_menu_item_new ();
        	gtk_menu_attach (GTK_MENU (menu), GTK_WIDGET (menuitem),
                         0, ROW_SPAN - 1, y, y + 1);
			gtk_widget_show (menuitem);
			y++;
		}
		menuitem=gtk_menu_item_new_with_label(_("Browse for more pictures..."));
		gtk_menu_attach (GTK_MENU (menu), GTK_WIDGET (menuitem),
                         0, ROW_SPAN - 1, y, y + 1);
		g_signal_connect (G_OBJECT (menuitem), "activate",
                          G_CALLBACK (file_icon_selected), NULL);
		gtk_widget_show (menuitem);
		photo_popup=menu;
		
		g_signal_connect (menu, "unmap",G_CALLBACK (on_photo_popup_unmap), togglebutton);
	}
	if(gtk_toggle_button_get_active(togglebutton))
	{
		gtk_menu_popup(GTK_MENU(photo_popup),NULL,NULL,
				(GtkMenuPositionFunc) popup_menu_below_button,
				GTK_WIDGET(togglebutton),
                0, gtk_get_current_event_time ());
	}
	else
	{
		gtk_menu_popdown(GTK_MENU(photo_popup));
	}
}

static gboolean
on_popup_button_button_pressed (GtkToggleButton *button,
                                GdkEventButton *event)
{
        if (event->button == 1) {
                if (!gtk_widget_get_visible (photo_popup)) {
                        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
                } else {
                        gtk_menu_popdown (GTK_MENU (photo_popup));
                        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);
                }

                return TRUE;
        }

        return FALSE;
}

void prepare_user_icon(GtkBuilder *builder)
{
	GtkWidget *w;
	w=(GtkWidget*)gtk_builder_get_object(builder,"user-icon-image");
	update_face_image(w);
	w=(GtkWidget*)gtk_builder_get_object(builder,"user-icon-button");
	g_signal_connect(w,"toggled",G_CALLBACK(on_user_icon_toggled),NULL);
}

void prepare_user_name(GtkBuilder *builder)
{
	GtkWidget *w;
	w=(GtkWidget*)gtk_builder_get_object(builder,"user-name");
	gtk_label_set_text(GTK_LABEL(w),user_name);
}

void on_user_autologin_toggled(GtkToggleButton *togglebutton,gpointer user_data)
{
	if(gtk_toggle_button_get_active(togglebutton))
		g_key_file_set_string(config,"base","autologin",user_name);
	else
		g_key_file_remove_key(config,"base","autologin",NULL);
	dirty++;
}

void prepare_user_autologin(GtkBuilder *builder)
{
	char *name;
	GtkWidget *w;
	w=(GtkWidget*)gtk_builder_get_object(builder,"user-autologin");
	if(!root) gtk_widget_set_sensitive(w,FALSE);
	g_signal_connect(w,"toggled",G_CALLBACK(on_user_autologin_toggled),NULL);
	name=g_key_file_get_string(config,"base","autologin",NULL);
	if(!name) return;
	if(!strcmp(name,user_name))
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),TRUE);
	}
	g_free(name);
}

static void on_bg_type_toggled(void)
{
	GtkWidget *w;
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-image-button");
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
	{
		char *s;
		w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-file");
		s=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w));
		if(s)
		{
			if(!image_file_valid(s))
			{
				g_free(s);
				s=g_key_file_get_string(config,"display","bg",NULL);
				if(s)
				{
					gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w),s);
					g_free(s);
				}
				return;
			}
			g_key_file_set_string(config,"display","bg",s);
			g_free(s);
		}
	}
	else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder,"lxdm-bg-color-button"))))
	{
		gchar *s;
		GdkColor color;
		w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-color");
		gtk_color_button_get_color(GTK_COLOR_BUTTON(w),&color);
		s=gdk_color_to_string(&color);
		g_key_file_set_string(config,"display","bg",s);
		g_free(s);
	}
	else
	{
		g_key_file_remove_key(config,"display","bg",NULL);
	}
	dirty++;
}

void prepare_bg(GtkBuilder *builder)
{
	char *s;
	GtkWidget *w;
	s=g_key_file_get_string(config,"display","bg",NULL);
	if(!s)
	{
		w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-default");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),TRUE);
	}
	else if(s[0]=='#')
	{
		GdkColor color;
		gdk_color_parse(s,&color);
		w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-color");
		gtk_color_button_set_color(GTK_COLOR_BUTTON(w),&color);
		w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-color-button");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),TRUE);
	}
	else
	{
		w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-file");
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w),s);
		w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-image-button");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),TRUE);
	}
	g_free(s);
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-image-button");
	if(!root) gtk_widget_set_sensitive(w,FALSE);
	g_signal_connect(w,"toggled",on_bg_type_toggled,NULL);
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-color-button");
	if(!root) gtk_widget_set_sensitive(w,FALSE);
	g_signal_connect(w,"toggled",on_bg_type_toggled,NULL);
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-file");
	if(!root) gtk_widget_set_sensitive(w,FALSE);
	g_signal_connect(w,"file-set",G_CALLBACK(on_bg_type_toggled),NULL);
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-color");
	if(!root) gtk_widget_set_sensitive(w,FALSE);
	g_signal_connect(w,"color-set",G_CALLBACK(on_bg_type_toggled),NULL);
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-bg-default");
	if(!root) gtk_widget_set_sensitive(w,FALSE);
	g_signal_connect(w,"toggled",on_bg_type_toggled,NULL);
}

static void on_enable_pane_toggled(GtkToggleButton *button)
{
        int val;
        val=gtk_toggle_button_get_active(button);
        g_key_file_set_integer(config,"display","bottom_pane",val);
        GtkWidget *w;
        w=(GtkWidget*)gtk_builder_get_object(builder,"vbox2");
        gtk_widget_set_sensitive(w,val?TRUE:FALSE);
	dirty++;
}

static void prepare_enable_pane(GtkBuilder *builder)
{
        gint val;
        GtkWidget *w;
        w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-enable-bottom-pane");
        val=g_key_file_get_integer(config,"display","bottom_pane",NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),val?TRUE:FALSE);
        if(!root) gtk_widget_set_sensitive(w,FALSE);
        g_signal_connect(w,"toggled",G_CALLBACK(on_enable_pane_toggled),NULL);
}

static void prepare_vbox2(GtkBuilder *builder)
{
        gint val;
        GtkWidget *w;
        w=(GtkWidget*)gtk_builder_get_object(builder,"vbox2");
        val=g_key_file_get_integer(config,"display","bottom_pane",NULL);
        if(!root) 
	{
		gtk_widget_set_sensitive(w,FALSE);
	}
	else
	{
		gtk_widget_set_sensitive(w,val?TRUE:FALSE);
	}
}

static void on_transparent_pane_toggled(GtkToggleButton *button)
{
        int val;
        val=gtk_toggle_button_get_active(button);
        g_key_file_set_integer(config,"display","transparent_pane",val);
        dirty++;
}

static void prepare_transparent_pane(GtkBuilder *builder)
{
        gint val;
        GtkWidget *w;
        w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-transparent-pane");
        val=g_key_file_get_integer(config,"display","transparent_pane",NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),val?TRUE:FALSE);
        if(!root) gtk_widget_set_sensitive(w,FALSE);
        g_signal_connect(w,"toggled",G_CALLBACK(on_transparent_pane_toggled),NULL);
}

static void on_hide_sessions_toggled(GtkToggleButton *button)
{
        int val;
        val=gtk_toggle_button_get_active(button);
        g_key_file_set_integer(config,"display","hide_sessions",val);
        dirty++;
}

static void prepare_hide_sessions(GtkBuilder *builder)
{
        gint val;
        GtkWidget *w;
        w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-hide-sessions");
        val=g_key_file_get_integer(config,"display","hide_sessions",NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),val?TRUE:FALSE);
        if(!root) gtk_widget_set_sensitive(w,FALSE);
        g_signal_connect(w,"toggled",G_CALLBACK(on_hide_sessions_toggled),NULL);
}

static void on_show_lang_toggled(GtkToggleButton *button)
{
	int val;
	val=gtk_toggle_button_get_active(button);
	g_key_file_set_integer(config,"display","lang",val);
	dirty++;
}

static void prepare_show_lang(GtkBuilder *builder)
{
	gint val;
	GtkWidget *w;
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-show-lang");
	val=g_key_file_get_integer(config,"display","lang",NULL);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),val?TRUE:FALSE);
	if(!root) gtk_widget_set_sensitive(w,FALSE);
	g_signal_connect(w,"toggled",G_CALLBACK(on_show_lang_toggled),NULL);
}

static void on_show_keyboard_toggled(GtkToggleButton *button)
{
	int val;
	val=gtk_toggle_button_get_active(button);
	g_key_file_set_integer(config,"display","keyboard",val);
	dirty++;
}

static void prepare_show_keyboard(GtkBuilder *builder)
{
	gint val;
	GtkWidget *w;
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-show-keyboard");
	val=g_key_file_get_integer(config,"display","keyboard",NULL);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),val?TRUE:FALSE);
	if(!root) gtk_widget_set_sensitive(w,FALSE);
	g_signal_connect(w,"toggled",G_CALLBACK(on_show_keyboard_toggled),NULL);
}

static void on_hide_exit_toggled(GtkToggleButton *button)
{
        int val;
        val=gtk_toggle_button_get_active(button);
        g_key_file_set_integer(config,"display","hide_exit",val);
        dirty++;
}

static void prepare_hide_exit(GtkBuilder *builder)
{
        gint val;
        GtkWidget *w;
        w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-hide-exit");
        val=g_key_file_get_integer(config,"display","hide_exit",NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),val?TRUE:FALSE);
        if(!root) gtk_widget_set_sensitive(w,FALSE);
        g_signal_connect(w,"toggled",G_CALLBACK(on_hide_exit_toggled),NULL);
}

static void on_hide_time_toggled(GtkToggleButton *button)
{
        int val;
        val=gtk_toggle_button_get_active(button);
        g_key_file_set_integer(config,"display","hide_time",val);
        dirty++;
}

static void prepare_hide_time(GtkBuilder *builder)
{
        gint val;
        GtkWidget *w;
        w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-hide-time");
        val=g_key_file_get_integer(config,"display","hide_time",NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),val?TRUE:FALSE);
        if(!root) gtk_widget_set_sensitive(w,FALSE);
        g_signal_connect(w,"toggled",G_CALLBACK(on_hide_time_toggled),NULL);
}

static void on_show_userlist_toggled(GtkToggleButton *button)
{
	int val;
	val=gtk_toggle_button_get_active(button);
	g_key_file_set_integer(config,"userlist","disable",!val);
	dirty++;
}

static void prepare_show_userlist(GtkBuilder *builder)
{
	gint val;
	GtkWidget *w;
	w=(GtkWidget*)gtk_builder_get_object(builder,"lxdm-user-list");
	val=g_key_file_get_integer(config,"userlist","disable",NULL);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),val?FALSE:TRUE);
	if(!root) gtk_widget_set_sensitive(w,FALSE);
	g_signal_connect(w,"toggled",G_CALLBACK(on_show_userlist_toggled),NULL);
}

GtkDialog *dialog_create(void)
{
	GtkDialog *dlg;
	
	builder=gtk_builder_new();
	gtk_builder_add_from_file(builder,CONFIG_UI_FILE,NULL);
	dlg=(GtkDialog*)gtk_builder_get_object(builder,"lxdm-config-dlg");
	if(!dlg) return NULL;
	prepare_user_icon(builder);
	prepare_user_name(builder);
	prepare_user_autologin(builder);
	prepare_bg(builder);
	prepare_enable_pane(builder);
	prepare_vbox2(builder);
	prepare_transparent_pane(builder);
	prepare_hide_sessions(builder);
	prepare_show_lang(builder);
	prepare_show_keyboard(builder);
	prepare_hide_exit(builder);
	prepare_hide_time(builder);
	prepare_show_userlist(builder);

	return dlg;
}

static GOptionEntry entries[] =
{
  { "user", 0, 0, G_OPTION_ARG_STRING, &user_name, "user name", NULL },
  { NULL }
};

int main(int arc,char *arg[])
{
	GError *error = NULL;
	GOptionContext *context;
	GtkDialog *dlg;
	
	context = g_option_context_new ("- lxdm config");
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	if (!g_option_context_parse (context, &arc, &arg, &error))
	{
		g_print ("option parsing failed: %s\n", error->message);
 		exit (1);
    }
    prepare_user();
	
#if !GTK_CHECK_VERSION(3,0,0)
	gtk_set_locale();
#endif
	bindtextdomain("lxdm", "/usr/share/locale");
	textdomain("lxdm");

	gtk_init(&arc,&arg);
	prepare_config();
	dlg=dialog_create();
	if(!dlg) exit(-1);
	gtk_dialog_run(dlg);
	
	if(dirty)
	{
		gsize length;
		gchar *s=g_key_file_to_data(config,&length,NULL);
		g_file_set_contents(CONFIG_FILE,s,length,NULL);
	}

	return 0;
}
