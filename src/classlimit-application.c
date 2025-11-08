/* classlimit-application.c
 *
 * Copyright 2025 Unknown
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"
#include <glib/gi18n.h>

#include "classlimit-application.h"
#include "classlimit-window.h"

struct _ClasslimitApplication
{
	AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE (ClasslimitApplication, classlimit_application, ADW_TYPE_APPLICATION)

ClasslimitApplication *
classlimit_application_new (const char        *application_id,
                            GApplicationFlags  flags)
{
	g_return_val_if_fail (application_id != NULL, NULL);

	return g_object_new (CLASSLIMIT_TYPE_APPLICATION,
	                     "application-id", application_id,
	                     "flags", flags,
	                     "resource-base-path", "/com/tomasps/classlimit",
	                     NULL);
}

static void
classlimit_application_activate (GApplication *app)
{
	GtkWindow *window;
	GtkCssProvider *css_provider;

	g_assert (CLASSLIMIT_IS_APPLICATION (app));

	window = gtk_application_get_active_window (GTK_APPLICATION (app));

	if (window == NULL) {
		window = g_object_new (CLASSLIMIT_TYPE_WINDOW,
		                       "application", app,
		                       NULL);
		
		/* Load custom CSS */
		css_provider = gtk_css_provider_new ();
		gtk_css_provider_load_from_resource (css_provider, "/com/tomasps/classlimit/style.css");
		gtk_style_context_add_provider_for_display (gtk_widget_get_display (GTK_WIDGET (window)),
		                                             GTK_STYLE_PROVIDER (css_provider),
		                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_unref (css_provider);
	}

	gtk_window_present (window);
}

static void
classlimit_application_class_init (ClasslimitApplicationClass *klass)
{
	GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

	app_class->activate = classlimit_application_activate;
}

static void classlimit_application_shortcuts_action (GSimpleAction *action,
													 GVariant      *parameter,
													 gpointer       user_data);

static void
classlimit_application_about_action (GSimpleAction *action,
                                     GVariant      *parameter,
                                     gpointer       user_data)
{
	static const char *developers[] = {"Tomas Palma Sanchez", NULL};
	ClasslimitApplication *self = user_data;
	GtkWindow *window = NULL;

	g_assert (CLASSLIMIT_IS_APPLICATION (self));

	window = gtk_application_get_active_window (GTK_APPLICATION (self));

	adw_show_about_dialog (GTK_WIDGET (window),
	                       "application-name", "classlimit",
	                       "application-icon", "com.tomasps.classlimit",
	                       "developer-name", "TomasPS",
	                       "translator-credits", _("translator-credits"),
	                       "version", "0.1",
	                       "developers", developers,
	                       "copyright", "© 2025 TomasPS",
	                       NULL);
}

static void
classlimit_application_quit_action (GSimpleAction *action,
                                    GVariant      *parameter,
                                    gpointer       user_data)
{
	ClasslimitApplication *self = user_data;

	g_assert (CLASSLIMIT_IS_APPLICATION (self));

	g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
	{ "quit", classlimit_application_quit_action },
	{ "about", classlimit_application_about_action },
};

static void
classlimit_application_init (ClasslimitApplication *self)
{
	GSimpleAction *shortcuts_action;

	g_action_map_add_action_entries (G_ACTION_MAP (self),
	                                 app_actions,
	                                 G_N_ELEMENTS (app_actions),
	                                 self);
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.quit",
	                                       (const char *[]) { "<control>q", NULL });

	/* shortcuts action will build dialog on demand */
	shortcuts_action = g_simple_action_new ("shortcuts", NULL);
	g_signal_connect (shortcuts_action, "activate", G_CALLBACK (classlimit_application_shortcuts_action), self);
	g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (shortcuts_action));
}

static void
classlimit_application_shortcuts_action (GSimpleAction *action,
										 GVariant      *parameter,
										 gpointer       user_data)
{
	ClasslimitApplication *self = CLASSLIMIT_APPLICATION (user_data);
	GtkWindow *win = gtk_application_get_active_window (GTK_APPLICATION (self));
	GtkBuilder *builder = gtk_builder_new_from_resource ("/com/tomasps/classlimit/shortcuts-dialog.ui");
	GtkWidget *dialog = GTK_WIDGET (gtk_builder_get_object (builder, "shortcuts_dialog"));
	gtk_window_set_transient_for (GTK_WINDOW (dialog), win);
	gtk_window_present (GTK_WINDOW (dialog));
	g_object_unref (builder);
}
