/* classlimit-window.c
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
#include <json-glib/json-glib.h>

#include "classlimit-window.h"

struct _ClasslimitWindow
{
	AdwApplicationWindow parent_instance;

	/* Template widgets */
	AdwViewStack   *view_stack;
	GtkListBox     *subjects_list;
	GtkEntry       *subject_name_entry;
	GtkSpinButton  *subject_hours_spin;
	GtkButton      *add_subject_button;
	GtkSpinButton  *percent_spin;
	GtkSpinButton  *weeks_spin;
	GtkSpinButton  *session_hours_spin;
	GtkButton      *calculate_button;
	GtkStack       *results_stack;
	GtkListBox     *results_list;
	GtkWidget      *results_page;

	/* Settings */
	GSettings      *settings;
};

G_DEFINE_FINAL_TYPE (ClasslimitWindow, classlimit_window, ADW_TYPE_APPLICATION_WINDOW)

static void recalc_results (ClasslimitWindow *self);

typedef struct {
	gchar *name;
	int weekly_hours;
	int current_skips;
	int total_classes;
	int allowed_skips;
} Subject;

static void subject_free (Subject *s) {
	if (!s) return;
	g_free (s->name);
	g_free (s);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Subject, subject_free)

static gboolean
animate_row_opacity (gpointer data)
{
	GtkWidget *widget = GTK_WIDGET (data);
	gdouble opacity = gtk_widget_get_opacity (widget);
	opacity += 0.1;
	if (opacity >= 1.0) {
		gtk_widget_set_opacity (widget, 1.0);
		return G_SOURCE_REMOVE;
	}
	gtk_widget_set_opacity (widget, opacity);
	return G_SOURCE_CONTINUE;
}

static void
on_remove_subject_clicked (GtkButton *b, gpointer user_data)
{
	ClasslimitWindow *self = CLASSLIMIT_WINDOW (user_data);
	GtkWidget *row = gtk_widget_get_ancestor (GTK_WIDGET (b), GTK_TYPE_LIST_BOX_ROW);
	GtkWidget *parent;
	if (!row)
		return;
	parent = gtk_widget_get_parent (row);
	if (GTK_IS_LIST_BOX (parent)) {
		gtk_list_box_remove (GTK_LIST_BOX (parent), row);
		/* Auto-recalculate after removal if currently on results page */
		if (adw_view_stack_get_visible_child (self->view_stack) == self->results_page)
			recalc_results (self);
	}
}

static void
on_skip_increment_clicked (GtkButton *b, gpointer user_data)
{
	GtkWidget *row = gtk_widget_get_ancestor (GTK_WIDGET (b), GTK_TYPE_LIST_BOX_ROW);
	Subject *s;
	GtkWidget *status_image;
	char subtitle[128];
	int remaining;
	
	if (!row) return;
	s = g_object_get_data (G_OBJECT (row), "subject");
	if (!s) return;
	
	s->current_skips++;
	
	remaining = s->allowed_skips - s->current_skips;
	if (s->allowed_skips > 0)
		g_snprintf (subtitle, sizeof subtitle, _("%d h/week • Skipped: %d • Remaining: %d"), s->weekly_hours, s->current_skips, remaining);
	else
		g_snprintf (subtitle, sizeof subtitle, _("%d h/week • Skipped: %d"), s->weekly_hours, s->current_skips);
	adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
	status_image = g_object_get_data (G_OBJECT (row), "status-image");
	if (GTK_IS_WIDGET (status_image)) {
		if (s->allowed_skips == 0)
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "view-statistics-symbolic");
		else if (remaining < 0)
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "dialog-error-symbolic");
		else if (remaining <= 2)
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "dialog-warning-symbolic");
		else
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "emblem-ok-symbolic");
	}
}

static void
on_skip_decrement_clicked (GtkButton *b, gpointer user_data)
{
	GtkWidget *row = gtk_widget_get_ancestor (GTK_WIDGET (b), GTK_TYPE_LIST_BOX_ROW);
	Subject *s;
	GtkWidget *status_image;
	char subtitle[128];
	int remaining;
	
	if (!row) return;
	s = g_object_get_data (G_OBJECT (row), "subject");
	if (!s) return;
	
	if (s->current_skips > 0)
		s->current_skips--;
	
	remaining = s->allowed_skips - s->current_skips;
	if (s->allowed_skips > 0)
		g_snprintf (subtitle, sizeof subtitle, _("%d h/week • Skipped: %d • Remaining: %d"), s->weekly_hours, s->current_skips, remaining);
	else
		g_snprintf (subtitle, sizeof subtitle, _("%d h/week • Skipped: %d"), s->weekly_hours, s->current_skips);
	adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
	status_image = g_object_get_data (G_OBJECT (row), "status-image");
	if (GTK_IS_WIDGET (status_image)) {
		if (s->allowed_skips == 0)
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "view-statistics-symbolic");
		else if (remaining < 0)
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "dialog-error-symbolic");
		else if (remaining <= 2)
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "dialog-warning-symbolic");
		else
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "emblem-ok-symbolic");
	}
}

static void
on_skip_reset_clicked (GtkButton *b, gpointer user_data)
{
	GtkWidget *row = gtk_widget_get_ancestor (GTK_WIDGET (b), GTK_TYPE_LIST_BOX_ROW);
	Subject *s;
	GtkWidget *status_image;
	char subtitle[128];
	
	if (!row) return;
	s = g_object_get_data (G_OBJECT (row), "subject");
	if (!s) return;
	
	s->current_skips = 0;
	
	if (s->allowed_skips > 0)
		g_snprintf (subtitle, sizeof subtitle, _("%d h/week • Skipped: %d • Remaining: %d"), s->weekly_hours, s->current_skips, s->allowed_skips);
	else
		g_snprintf (subtitle, sizeof subtitle, _("%d h/week • Skipped: %d"), s->weekly_hours, s->current_skips);
	adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
	status_image = g_object_get_data (G_OBJECT (row), "status-image");
	if (GTK_IS_WIDGET (status_image)) {
		if (s->allowed_skips == 0)
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "view-statistics-symbolic");
		else
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "emblem-ok-symbolic");
	}
}

static GtkWidget *
create_subject_row (Subject *s, ClasslimitWindow *self)
{
	GtkWidget *row = adw_action_row_new ();
	GtkWidget *controls;
	GtkWidget *btn_minus;
	GtkWidget *btn_plus;
	GtkWidget *btn_reset;
	GtkWidget *remove_btn;
	GtkWidget *status_image;
	char subtitle[64];

	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), s->name);
	g_snprintf (subtitle, sizeof subtitle, _("%d h/week"), s->weekly_hours);
	adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);

	/* Status indicator (updated after calculation) */
	status_image = gtk_image_new_from_icon_name ("view-statistics-symbolic");
	gtk_widget_add_css_class (status_image, "dim-label");
	adw_action_row_add_suffix (ADW_ACTION_ROW (row), status_image);

	/* Linked controls for skip tracking */
	controls = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_add_css_class (controls, "linked");

	btn_minus = gtk_button_new_from_icon_name ("list-remove-symbolic");
	gtk_widget_set_tooltip_text (btn_minus, _("Decrease skip count"));
	gtk_widget_add_css_class (btn_minus, "flat");
	adw_action_row_add_suffix (ADW_ACTION_ROW (row), controls);
	gtk_box_append (GTK_BOX (controls), btn_minus);

	btn_plus = gtk_button_new_from_icon_name ("list-add-symbolic");
	gtk_widget_set_tooltip_text (btn_plus, _("Increase skip count"));
	gtk_widget_add_css_class (btn_plus, "flat");
	gtk_box_append (GTK_BOX (controls), btn_plus);

	btn_reset = gtk_button_new_from_icon_name ("edit-clear-all-symbolic");
	gtk_widget_set_tooltip_text (btn_reset, _("Reset skip count"));
	gtk_widget_add_css_class (btn_reset, "flat");
	gtk_box_append (GTK_BOX (controls), btn_reset);

	/* Remove button at the end */
	remove_btn = gtk_button_new_from_icon_name ("user-trash-symbolic");
	gtk_widget_set_tooltip_text (remove_btn, _("Remove subject"));
	gtk_widget_add_css_class (remove_btn, "flat");
	adw_action_row_add_suffix (ADW_ACTION_ROW (row), remove_btn);

	/* Data attachments */
	g_object_set_data_full (G_OBJECT (row), "subject", s, (GDestroyNotify) subject_free);
	g_object_set_data (G_OBJECT (row), "status-image", status_image);

	/* Signals */
	g_signal_connect (remove_btn, "clicked", G_CALLBACK (on_remove_subject_clicked), self);
	g_signal_connect (btn_plus, "clicked", G_CALLBACK (on_skip_increment_clicked), self);
	g_signal_connect (btn_minus, "clicked", G_CALLBACK (on_skip_decrement_clicked), self);
	g_signal_connect (btn_reset, "clicked", G_CALLBACK (on_skip_reset_clicked), self);

	return row;
}

static void
on_add_subject_clicked (GtkButton *btn, gpointer user_data)
{
	ClasslimitWindow *self = CLASSLIMIT_WINDOW (user_data);
	const char *name = gtk_editable_get_text (GTK_EDITABLE (self->subject_name_entry));
	int hours;
	g_autoptr(Subject) s;
	GtkWidget *row;
	
	if (!name || !*name) return;
	hours = gtk_spin_button_get_value_as_int (self->subject_hours_spin);
	if (hours <= 0) return;

	s = g_new0 (Subject, 1);
	s->name = g_strdup (name);
	s->weekly_hours = hours;
	s->current_skips = 0;
	s->total_classes = 0;
	s->allowed_skips = 0;
	row = create_subject_row (s, self);
	/* ownership of s transferred to row data */
	g_steal_pointer (&s);
	gtk_list_box_append (self->subjects_list, row);
	gtk_editable_set_text (GTK_EDITABLE (self->subject_name_entry), "");
	gtk_spin_button_set_value (self->subject_hours_spin, 0);
	gtk_widget_grab_focus (GTK_WIDGET (self->subject_name_entry));
}

static void
clear_results (ClasslimitWindow *self)
{
	GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self->results_list));
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		gtk_list_box_remove (self->results_list, child);
		child = next;
	}
}

static void
recalc_results (ClasslimitWindow *self)
{
	GtkWidget *row;
	int weeks;
	int required_pct;
	int allowed_pct;
	int total_allowed_all = 0;
	int total_classes_all = 0;
	int session_hours;
	
	/* Add subtle animation when recalculating */
	gtk_widget_set_sensitive (GTK_WIDGET (self->calculate_button), FALSE);
	
	clear_results (self);
	weeks = gtk_spin_button_get_value_as_int (self->weeks_spin);
	required_pct = gtk_spin_button_get_value_as_int (self->percent_spin); /* attendance required */
	session_hours = gtk_spin_button_get_value_as_int (self->session_hours_spin);
	if (required_pct < 1) required_pct = 1;
	if (session_hours < 1) session_hours = 1;
	allowed_pct = 100 - required_pct; /* absence percentage allowed */

	for (row = gtk_widget_get_first_child (GTK_WIDGET (self->subjects_list));
		 row != NULL;
		 row = gtk_widget_get_next_sibling (row)) {
		Subject *s = g_object_get_data (G_OBJECT (row), "subject");
		GtkWidget *result_row;
		char buf[256];
		char detail[128];
		int total_classes, allowed_skip, total_sessions, allowed_skip_sessions;
		int remaining;

		if (!s) continue;
		total_classes = s->weekly_hours * weeks;
		allowed_skip = (total_classes * allowed_pct) / 100; /* floor */
		total_sessions = total_classes / session_hours;
		allowed_skip_sessions = allowed_skip / session_hours;

		/* Store calculated values */
		s->total_classes = total_classes;
		s->allowed_skips = (session_hours > 1) ? allowed_skip_sessions : allowed_skip;
		remaining = s->allowed_skips - s->current_skips;

		result_row = adw_action_row_new ();
		g_snprintf (buf, sizeof buf, _("%s"), s->name);
		adw_preferences_row_set_title (ADW_PREFERENCES_ROW (result_row), buf);
		if (session_hours > 1)
			g_snprintf (detail, sizeof detail, _("%d sessions allowed • %d total sessions"), allowed_skip_sessions, total_sessions);
		else
			g_snprintf (detail, sizeof detail, _("%d classes allowed • %d total classes"), allowed_skip, total_classes);
		adw_action_row_set_subtitle (ADW_ACTION_ROW (result_row), detail);
		GtkWidget *status_image = gtk_image_new_from_icon_name ("emblem-ok-symbolic");
		if (remaining < 0)
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "dialog-error-symbolic");
		else if (remaining <= 2)
			gtk_image_set_from_icon_name (GTK_IMAGE (status_image), "dialog-warning-symbolic");
		adw_action_row_add_suffix (ADW_ACTION_ROW (result_row), status_image);
		gtk_list_box_append (self->results_list, result_row);

		/* Update subject row subtitle + status */
		{
			char subbuf[128];
			GtkWidget *subject_status = g_object_get_data (G_OBJECT (row), "status-image");
			if (s->allowed_skips > 0)
				g_snprintf (subbuf, sizeof subbuf, _("%d h/week • Skipped: %d • Remaining: %d"), s->weekly_hours, s->current_skips, remaining);
			else
				g_snprintf (subbuf, sizeof subbuf, _("%d h/week"), s->weekly_hours);
			adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subbuf);
			if (GTK_IS_WIDGET (subject_status)) {
				if (remaining < 0)
					gtk_image_set_from_icon_name (GTK_IMAGE (subject_status), "dialog-error-symbolic");
				else if (remaining <= 2)
					gtk_image_set_from_icon_name (GTK_IMAGE (subject_status), "dialog-warning-symbolic");
				else
					gtk_image_set_from_icon_name (GTK_IMAGE (subject_status), "emblem-ok-symbolic");
			}
		}

		total_allowed_all += allowed_skip;
		total_classes_all += total_classes;
	}
	if (total_classes_all > 0) {
		char summary[256];
		GtkWidget *summary_row;
		GtkWidget *summary_box;
		GtkWidget *lbl_sum;
		GtkWidget *lbl_sum_detail;
		int total_sessions_all;
		int allowed_sessions_all;
		char detail[128];
		
		total_sessions_all = total_classes_all / session_hours;
		allowed_sessions_all = total_allowed_all / session_hours;
		
		if (session_hours > 1) {
			g_snprintf (summary, sizeof summary, _("Total: %d sessions allowed to skip"), allowed_sessions_all);
		} else {
			g_snprintf (summary, sizeof summary, _("Total: %d classes allowed to skip"), total_allowed_all);
		}
		
		summary_row = gtk_list_box_row_new();
		summary_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
		lbl_sum = gtk_label_new (summary);
		gtk_label_set_xalign (GTK_LABEL (lbl_sum), 0.0);
		gtk_widget_add_css_class (lbl_sum, "title-3");
		
		if (session_hours > 1) {
			g_snprintf (detail, sizeof detail, _("Out of %d total sessions (%d%% attendance required)"), 
				total_sessions_all, required_pct);
		} else {
			g_snprintf (detail, sizeof detail, _("Out of %d total classes (%d%% attendance required)"), 
				total_classes_all, required_pct);
		}
		lbl_sum_detail = gtk_label_new (detail);
		gtk_label_set_xalign (GTK_LABEL (lbl_sum_detail), 0.0);
		gtk_widget_add_css_class (lbl_sum_detail, "caption");
		
		gtk_box_append (GTK_BOX (summary_box), lbl_sum);
		gtk_box_append (GTK_BOX (summary_box), lbl_sum_detail);
		gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (summary_row), summary_box);
		gtk_list_box_prepend (self->results_list, summary_row);
	}
	/* Show results list on results page */
	gtk_stack_set_visible_child (self->results_stack, GTK_WIDGET (self->results_list));
	adw_view_stack_set_visible_child (self->view_stack, self->results_page);
	
	/* Re-enable button after calculation */
	gtk_widget_set_sensitive (GTK_WIDGET (self->calculate_button), TRUE);
}

static void
on_calculate_clicked (GtkButton *btn, gpointer user_data)
{
	recalc_results (CLASSLIMIT_WINDOW (user_data));
}

static void
save_subjects_to_settings (ClasslimitWindow *self)
{
	GVariantBuilder builder;
	GtkWidget *row;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(siii)"));

	for (row = gtk_widget_get_first_child (GTK_WIDGET (self->subjects_list));
		 row != NULL;
		 row = gtk_widget_get_next_sibling (row)) {
		Subject *s = g_object_get_data (G_OBJECT (row), "subject");
		if (!s) continue;
		g_variant_builder_add (&builder, "(siii)", 
			s->name, s->weekly_hours, s->current_skips, s->allowed_skips);
	}

	g_settings_set_value (self->settings, "subjects", g_variant_builder_end (&builder));
	g_settings_set_int (self->settings, "required-attendance", 
		gtk_spin_button_get_value_as_int (self->percent_spin));
	g_settings_set_int (self->settings, "total-weeks", 
		gtk_spin_button_get_value_as_int (self->weeks_spin));
	g_settings_set_int (self->settings, "session-hours", 
		gtk_spin_button_get_value_as_int (self->session_hours_spin));
}

static void
load_subjects_from_settings (ClasslimitWindow *self)
{
	GVariant *subjects_var = g_settings_get_value (self->settings, "subjects");
	GVariantIter iter;
	const gchar *name;
	gint weekly_hours, current_skips, allowed_skips;

	g_variant_iter_init (&iter, subjects_var);
	while (g_variant_iter_next (&iter, "(siii)", &name, &weekly_hours, &current_skips, &allowed_skips)) {
		Subject *s;
		GtkWidget *row;
		
		s = g_new0 (Subject, 1);
		s->name = g_strdup (name);
		s->weekly_hours = weekly_hours;
		s->current_skips = current_skips;
		s->allowed_skips = allowed_skips;
		row = create_subject_row (s, self);
		gtk_list_box_append (self->subjects_list, row);
		
		/* Initialize subtitle with current skip state */
		{
			char subtitle[128];
			if (s->allowed_skips > 0) {
				int remaining = s->allowed_skips - s->current_skips;
				g_snprintf (subtitle, sizeof subtitle, _("%d h/week • Skipped: %d • Remaining: %d"), weekly_hours, current_skips, remaining);
			} else {
				g_snprintf (subtitle, sizeof subtitle, _("%d h/week • Skipped: %d"), weekly_hours, current_skips);
			}
			adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
			GtkWidget *status = g_object_get_data (G_OBJECT (row), "status-image");
			if (GTK_IS_WIDGET (status)) {
				if (s->allowed_skips == 0)
					gtk_image_set_from_icon_name (GTK_IMAGE (status), "view-statistics-symbolic");
				else
					gtk_image_set_from_icon_name (GTK_IMAGE (status), "emblem-ok-symbolic");
			}
		}
	}
	g_variant_unref (subjects_var);

	gtk_spin_button_set_value (self->percent_spin, 
		g_settings_get_int (self->settings, "required-attendance"));
	gtk_spin_button_set_value (self->weeks_spin, 
		g_settings_get_int (self->settings, "total-weeks"));
	gtk_spin_button_set_value (self->session_hours_spin, 
		g_settings_get_int (self->settings, "session-hours"));
}

static void
on_reset_all_action (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ClasslimitWindow *self = CLASSLIMIT_WINDOW (user_data);
	
	/* Clear all subjects */
	GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (self->subjects_list));
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		gtk_list_box_remove (self->subjects_list, child);
		child = next;
	}
	
	/* Clear results */
	clear_results (self);
	gtk_stack_set_visible_child (self->results_stack, gtk_widget_get_first_child (GTK_WIDGET (self->results_stack)));
	adw_view_stack_set_visible_child_name (self->view_stack, "subjects");
	
	/* Reset spin buttons to defaults */
	gtk_spin_button_set_value (self->percent_spin, 80);
	gtk_spin_button_set_value (self->weeks_spin, 15);
	gtk_spin_button_set_value (self->session_hours_spin, 1);
	
	/* Save empty state */
	save_subjects_to_settings (self);
}

static void
on_export_finished (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GFile *file = G_FILE (source);
	GError *error = NULL;
	
	if (!g_file_replace_contents_finish (file, result, NULL, &error)) {
		g_warning ("Failed to export: %s", error->message);
		g_error_free (error);
	}
}

static void
on_export_save_callback (GObject *source, GAsyncResult *result, gpointer user_data)
{
	ClasslimitWindow *self = CLASSLIMIT_WINDOW (user_data);
	GtkFileDialog *dialog = GTK_FILE_DIALOG (source);
	GError *error = NULL;
	GFile *file = gtk_file_dialog_save_finish (dialog, result, &error);
	JsonBuilder *builder;
	JsonNode *root;
	JsonGenerator *gen;
	gchar *json_data;
	GtkWidget *row;
	
	if (!file) {
		if (error && !g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
			g_warning ("Export cancelled or failed: %s", error->message);
		g_clear_error (&error);
		return;
	}
	
	/* Build JSON */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "version");
	json_builder_add_int_value (builder, 1);
	json_builder_set_member_name (builder, "required_attendance");
	json_builder_add_int_value (builder, gtk_spin_button_get_value_as_int (self->percent_spin));
	json_builder_set_member_name (builder, "total_weeks");
	json_builder_add_int_value (builder, gtk_spin_button_get_value_as_int (self->weeks_spin));
	json_builder_set_member_name (builder, "session_hours");
	json_builder_add_int_value (builder, gtk_spin_button_get_value_as_int (self->session_hours_spin));
	json_builder_set_member_name (builder, "subjects");
	json_builder_begin_array (builder);
	
	for (row = gtk_widget_get_first_child (GTK_WIDGET (self->subjects_list));
		 row != NULL; row = gtk_widget_get_next_sibling (row)) {
		Subject *s = g_object_get_data (G_OBJECT (row), "subject");
		if (!s) continue;
		
		json_builder_begin_object (builder);
		json_builder_set_member_name (builder, "name");
		json_builder_add_string_value (builder, s->name);
		json_builder_set_member_name (builder, "weekly_hours");
		json_builder_add_int_value (builder, s->weekly_hours);
		json_builder_set_member_name (builder, "current_skips");
		json_builder_add_int_value (builder, s->current_skips);
		json_builder_end_object (builder);
	}
	
	json_builder_end_array (builder);
	json_builder_end_object (builder);
	
	root = json_builder_get_root (builder);
	gen = json_generator_new ();
	json_generator_set_root (gen, root);
	json_generator_set_pretty (gen, TRUE);
	json_data = json_generator_to_data (gen, NULL);
	
	g_file_replace_contents_async (file, json_data, strlen (json_data),
		NULL, FALSE, G_FILE_CREATE_NONE, NULL, on_export_finished, NULL);
	
	g_free (json_data);
	json_node_free (root);
	g_object_unref (gen);
	g_object_unref (builder);
	g_object_unref (file);
}

static void
on_export_action (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ClasslimitWindow *self = CLASSLIMIT_WINDOW (user_data);
	GtkFileDialog *dialog = gtk_file_dialog_new ();
	
	gtk_file_dialog_set_title (dialog, _("Export Subjects"));
	gtk_file_dialog_set_initial_name (dialog, "classlimit-subjects.json");
	
	gtk_file_dialog_save (dialog, GTK_WINDOW (self), NULL, 
		on_export_save_callback, self);
}

static void
on_import_open_callback (GObject *source, GAsyncResult *result, gpointer user_data)
{
	ClasslimitWindow *self = CLASSLIMIT_WINDOW (user_data);
	GtkFileDialog *dialog = GTK_FILE_DIALOG (source);
	GError *error = NULL;
	GFile *file = gtk_file_dialog_open_finish (dialog, result, &error);
	gchar *contents = NULL;
	gsize length;
	JsonParser *parser;
	JsonNode *root;
	JsonObject *obj;
	JsonArray *subjects;
	guint i;
	GtkWidget *child;
	
	if (!file) {
		if (error && !g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
			g_warning ("Import cancelled or failed: %s", error->message);
		g_clear_error (&error);
		return;
	}
	
	if (!g_file_load_contents (file, NULL, &contents, &length, NULL, &error)) {
		g_warning ("Failed to read file: %s", error->message);
		g_error_free (error);
		g_object_unref (file);
		return;
	}
	
	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, contents, length, &error)) {
		g_warning ("Failed to parse JSON: %s", error->message);
		g_error_free (error);
		g_free (contents);
		g_object_unref (parser);
		g_object_unref (file);
		return;
	}
	
	root = json_parser_get_root (parser);
	obj = json_node_get_object (root);
	
	/* Clear existing subjects */
	child = gtk_widget_get_first_child (GTK_WIDGET (self->subjects_list));
	while (child) {
		GtkWidget *next = gtk_widget_get_next_sibling (child);
		gtk_list_box_remove (self->subjects_list, child);
		child = next;
	}
	
	/* Load settings */
	if (json_object_has_member (obj, "required_attendance"))
		gtk_spin_button_set_value (self->percent_spin, json_object_get_int_member (obj, "required_attendance"));
	if (json_object_has_member (obj, "total_weeks"))
		gtk_spin_button_set_value (self->weeks_spin, json_object_get_int_member (obj, "total_weeks"));
	if (json_object_has_member (obj, "session_hours"))
		gtk_spin_button_set_value (self->session_hours_spin, json_object_get_int_member (obj, "session_hours"));
	
	/* Load subjects */
	subjects = json_object_get_array_member (obj, "subjects");
	for (i = 0; i < json_array_get_length (subjects); i++) {
		JsonObject *subj_obj = json_array_get_object_element (subjects, i);
		Subject *s = g_new0 (Subject, 1);
		GtkWidget *row;
		s->name = g_strdup (json_object_get_string_member (subj_obj, "name"));
		s->weekly_hours = json_object_get_int_member (subj_obj, "weekly_hours");
		s->current_skips = json_object_has_member (subj_obj, "current_skips") ? 
			json_object_get_int_member (subj_obj, "current_skips") : 0;
		s->allowed_skips = 0;
		s->total_classes = 0;
		
		row = create_subject_row (s, self);
		gtk_list_box_append (self->subjects_list, row);
	}
	
	save_subjects_to_settings (self);
	
	g_free (contents);
	g_object_unref (parser);
	g_object_unref (file);
}

static void
on_import_action (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	ClasslimitWindow *self = CLASSLIMIT_WINDOW (user_data);
	GtkFileDialog *dialog = gtk_file_dialog_new ();
	
	gtk_file_dialog_set_title (dialog, _("Import Subjects"));
	
	gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL,
		on_import_open_callback, self);
}

static void
classlimit_window_class_init (ClasslimitWindowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/com/tomasps/classlimit/classlimit-window.ui");
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, view_stack);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, subjects_list);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, subject_name_entry);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, subject_hours_spin);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, add_subject_button);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, percent_spin);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, weeks_spin);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, session_hours_spin);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, calculate_button);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, results_stack);
	gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, results_list);
    gtk_widget_class_bind_template_child (widget_class, ClasslimitWindow, results_page);
}

static void
on_onboarding_close (AdwDialog *dialog, gpointer user_data)
{
	ClasslimitWindow *self = CLASSLIMIT_WINDOW (user_data);
	g_settings_set_boolean (self->settings, "onboarding-completed", TRUE);
}

static void
show_onboarding_if_needed (ClasslimitWindow *self)
{
	gboolean onboarding_completed;
	GtkBuilder *builder;
	AdwDialog *dialog;
	GtkButton *get_started_button;
	
	onboarding_completed = g_settings_get_boolean (self->settings, "onboarding-completed");
	
	if (onboarding_completed)
		return;
	
	builder = gtk_builder_new_from_resource ("/com/tomasps/classlimit/onboarding-dialog.ui");
	dialog = ADW_DIALOG (gtk_builder_get_object (builder, "onboarding_dialog"));
	get_started_button = GTK_BUTTON (gtk_builder_get_object (builder, "get_started_button"));
	
	g_signal_connect_swapped (get_started_button, "clicked", 
		G_CALLBACK (adw_dialog_close), dialog);
	g_signal_connect (dialog, "closed", 
		G_CALLBACK (on_onboarding_close), self);
	
	adw_dialog_present (dialog, GTK_WIDGET (self));
	g_object_unref (builder);
}

static void
classlimit_window_init (ClasslimitWindow *self)
{
	GSimpleAction *reset_action;
	GSimpleAction *export_action;
	GSimpleAction *import_action;

	gtk_widget_init_template (GTK_WIDGET (self));
	
	/* Initialize GSettings */
	self->settings = g_settings_new ("com.tomasps.classlimit");
	
	/* Load saved data */
	load_subjects_from_settings (self);
	
	/* Connect signals */
	g_signal_connect (self->add_subject_button, "clicked", G_CALLBACK (on_add_subject_clicked), self);
	g_signal_connect (self->calculate_button, "clicked", G_CALLBACK (on_calculate_clicked), self);
	
	/* Auto-save on changes */
	g_signal_connect_swapped (self->percent_spin, "value-changed", 
		G_CALLBACK (save_subjects_to_settings), self);
	g_signal_connect_swapped (self->weeks_spin, "value-changed", 
		G_CALLBACK (save_subjects_to_settings), self);
	g_signal_connect_swapped (self->session_hours_spin, "value-changed", 
		G_CALLBACK (save_subjects_to_settings), self);
	
	/* Add actions */
	reset_action = g_simple_action_new ("reset-all", NULL);
	g_signal_connect (reset_action, "activate", G_CALLBACK (on_reset_all_action), self);
	g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (reset_action));
	
	export_action = g_simple_action_new ("export", NULL);
	g_signal_connect (export_action, "activate", G_CALLBACK (on_export_action), self);
	g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (export_action));
	
	import_action = g_simple_action_new ("import", NULL);
	g_signal_connect (import_action, "activate", G_CALLBACK (on_import_action), self);
	g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (import_action));
	
	/* Show onboarding if this is first launch */
	g_idle_add_once ((GSourceOnceFunc) show_onboarding_if_needed, self);
}
