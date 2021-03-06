/*
 * debug.c
 * This file is part of codefox
 *
 * Copyright (C) 2012-2017 - Gordon Li
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib/gi18n-lib.h>

#include "debug.h"
#include "ui.h"
#include "misc.h"
#include "env.h"
#include "limits.h"

#define GDB_SERVER_PORT_ARG ":10086"

static gint fd1[2];
static gint fd2[2];
static FILE *out_file;
static FILE *in_file;
static GList *breakpoint_list;
static gboolean debugging;
static pid_t target_pid;
static pid_t gdb_pid;
static pid_t proc_pid;

static GMutex debug_mutex;

static void debug_breakpoint_list_clear ();
static void debug_breakpoint_add (const gchar *breakpoint_desc);
static gboolean debug_conection_broken (const gchar *output);

static void
debug_breakpoint_list_clear ()
{
	GList * iterator;

	for (iterator = breakpoint_list; iterator; iterator = iterator->next) {
		CBreakPoint *breakpoint;

		breakpoint = (CBreakPoint *) iterator->data;
		
		g_free (breakpoint->filepath);
		g_free (breakpoint);
	}

	g_list_free (breakpoint_list);
	breakpoint_list = NULL;
}

static void
debug_breakpoint_add (const gchar *breakpoint_desc)
{
	CBreakPoint *breakpoint;
	gchar gdb_bp[MAX_FILEPATH_LENGTH + 1];

	breakpoint = (CBreakPoint *) g_malloc (sizeof (CBreakPoint));
	breakpoint->filepath = (gchar *) g_malloc (MAX_FILEPATH_LENGTH + 1);
	sscanf (breakpoint_desc, "%s %d", breakpoint->filepath, &(breakpoint->line));

	breakpoint_list = g_list_append (breakpoint_list, (gpointer) breakpoint);

	g_snprintf (gdb_bp, MAX_FILEPATH_LENGTH, "%s:%d", breakpoint->filepath, breakpoint->line);
	debug_command_exec ("b", gdb_bp, NULL);
}

static gboolean 
debug_conection_broken (const gchar *UNUSED)
{
	return !target_pid || kill (target_pid, 0) != 0
		   || !gdb_pid || kill (gdb_pid, 0) != 0
		   || !proc_pid || kill (proc_pid, 0) != 0;
}

static void
debug_skip_startup_output ()
{
	gchar line[MAX_LINE_LENGTH + 1];
	gchar *ret;

	for (;;) {
		line[0] = 0;
		errno = 0;
		ret = fgets (line, MAX_LINE_LENGTH, out_file);
		if (errno == EAGAIN && ret == NULL) {
			continue;
		}
		if (g_str_has_prefix (line, "(gdb)")) {
			break;
		}
	}
}

void
debug_startup (const gchar *project_path, const gchar *project_name)
{
	gchar exe_path[MAX_FILEPATH_LENGTH + 1];
	gint ret;

	if (!env_prog_exist (ENV_PROG_XTERM) 
		|| !env_prog_exist (ENV_PROG_GDB)
		|| !env_prog_exist (ENV_PROG_GDBSERVER)) {
		g_warning ("xterm gdb or gdbserver not found.");

		return;
	}

	target_pid = (pid_t) 0;
	gdb_pid = (pid_t) 0;
	proc_pid = (pid_t) 0;

	g_snprintf (exe_path, MAX_FILEPATH_LENGTH, "%s/%s", project_path, project_name);

	target_pid = fork ();
	if (target_pid == 0) {
		execlp ("xterm", "xterm", "-e", "gdbserver", GDB_SERVER_PORT_ARG, exe_path, NULL);
	}

	ret = pipe (fd1);
	if (ret == -1) {
		g_error ("failed to pipe fd1.");

		return;
	}

	ret = pipe (fd2);
	if (ret == -1) {
		g_error ("failed to pipe fd2.");

		return;
	}

	gdb_pid = fork ();
	if (gdb_pid == 0) {

		close(fd1[1]);
		close(fd2[0]);

		fflush (stdin);
		fflush (stdout);
		dup2 (fd1[0], STDIN_FILENO);
		dup2 (fd2[1], STDOUT_FILENO);
		dup2 (fd2[1], STDERR_FILENO);
		close (fd1[0]);
		close (fd2[1]);

		execlp ("gdb", "gdb", "--quiet", "--interpreter=mi", exe_path, NULL);
	}
	else {
		gint old_fl;

		close(fd1[0]);
		close(fd2[1]);

		out_file = fdopen (fd2[0], "r");
		in_file = fdopen (fd1[1], "w");

		old_fl = fcntl (fd2[0], F_GETFL, 0);
		fcntl (fd2[0], F_SETFL, old_fl | O_NONBLOCK);
		setvbuf (in_file, NULL, _IONBF, 0);

		debug_skip_startup_output ();
		debug_breakpoint_list_clear ();

		g_mutex_lock (&debug_mutex);
		debugging = TRUE;
		g_mutex_unlock (&debug_mutex);
	}
}

static void
debug_parse_mi_line (gchar *line)
{
	gchar temp[MAX_LINE_LENGTH + 1];
	gint i;
	gint j;
	gint len;

	i = 0;
	j = 0;
	len = strlen (line);
	i += 2;
	len -= 2;
	while (i < len) {
		if (line[i] == '\\' && i + 1 < len) {
			gint ok = 1;
			
			switch (line[i + 1]) {
			case 'n':
				temp[j++] = '\n';
				break;
			case 't':
				temp[j++] = '\t';
				break;
			case '\'':
			case '\"':
			case '\\':
				temp[j++] = line[i + 1];
				break;
			default:
				g_warning ("unknown mi escape char: \\%c.", line[i + 1]);
				ok = 0;
				break;
			}
			if (ok) {
				i += 2;
				continue;
			}
		}
		temp[j++] = line[i++];
	}
	temp[j] = 0;
	g_strlcpy (line, temp, MAX_LINE_LENGTH);
}

void
debug_command_exec (const gchar *command, const gchar *para, gchar *output)
{
	gchar line[MAX_LINE_LENGTH + 1];
	gint ret;
	gchar *ret2;
	gint start;
	gchar cmd_line[MAX_LINE_LENGTH + 1];

	g_mutex_lock (&debug_mutex);
	if (!debugging) {
		g_mutex_unlock (&debug_mutex);

		return;
	}

	fflush (out_file);

	g_strlcpy (cmd_line, command, MAX_LINE_LENGTH);
	if (para != NULL) {
		g_strlcat (cmd_line, " ", MAX_LINE_LENGTH);
		g_strlcat (cmd_line, para, MAX_LINE_LENGTH);
	}
	g_strlcat (cmd_line, "\n", MAX_LINE_LENGTH);

	ret = write (fd1[1], cmd_line, strlen (cmd_line));
	if (ret == -1) {
		g_error ("failed to write command and args to gdb pipe.");

		g_mutex_unlock (&debug_mutex);

		return;
	}

	line[0] = 0;
	if (output != NULL) {
		output[0] = 0;
	}

	start = 0;
	for (;;) {
		line[0] = 0;
		errno = 0;
		ret2 = fgets (line, MAX_LINE_LENGTH, out_file);
		if (errno == EAGAIN && ret2 == NULL) {
			continue;
		}
		if (!start && (!g_str_has_prefix (line, "&") || !g_str_has_prefix (line + 2, command))) {
			continue;
		}
		start = 1;
		if (g_str_has_prefix (line, "(gdb)")) {
			break;
		}
		if (strlen (line) > 0 && line[0] == '~') {
			debug_parse_mi_line (line);
		}
		else {
			continue;
		}
		if (output != NULL) {
			g_strlcat (output, line, MAX_RESULT_LENGTH);
		}
	}

	g_mutex_unlock (&debug_mutex);
}

void
debug_breakpoints_insert (GList *list)
{
	GList * iterator;

	if (!debug_is_active ()) {
		return;
	}

	for (iterator = list; iterator; iterator = iterator->next) {
		gchar *breakpoint_desc;

		breakpoint_desc = (gchar *) iterator->data;
		debug_breakpoint_add (breakpoint_desc);
	}
}

void
debug_connect (const gchar *project_path, const gchar *project_name)
{
	gchar exe_path[MAX_FILEPATH_LENGTH + 1];
	gchar *output;
	gint i;
	gint pid;
	gint len;

	output = (gchar *) g_malloc (MAX_RESULT_LENGTH + 1);

	g_snprintf (exe_path, MAX_FILEPATH_LENGTH, "%s/%s", project_path, project_name);

	debug_command_exec ("target remote", GDB_SERVER_PORT_ARG, NULL);
	debug_command_exec ("info", "proc", output);
	i = 0;
	len = strlen (output);
	while (i < len && !g_str_has_prefix (output + i, "process")) {
		i++;
	}
	if (i >= len) {
		g_warning ("can't get gdbserver pid.");
		kill (gdb_pid, SIGKILL);
		kill (target_pid, SIGKILL);

		g_mutex_lock (&debug_mutex);
		debugging = FALSE;
		g_mutex_unlock (&debug_mutex);

		g_free ((gpointer) output);

		return;
	}
	i += strlen ("process") + 1;
	sscanf (output + i, "%d", &pid);
	proc_pid = (pid_t) pid;

	g_free ((gpointer) output);
}

void
debug_breakpoint_update (gchar *breakpoint_desc)
{
	GList *iterator;
	CBreakPoint *breakpoint;
	gchar filepath[MAX_FILEPATH_LENGTH + 1];
	gint line;

	if (!debugging) {
		return;
	}

	sscanf (breakpoint_desc, "%s %d", filepath, &line);
	for (iterator = breakpoint_list; iterator; iterator = iterator->next) {
		breakpoint = (CBreakPoint *) iterator->data;

		if (g_strcmp0 (filepath, breakpoint->filepath) == 0 && line == breakpoint->line) {
			breakpoint_list = g_list_remove (breakpoint_list, (gpointer) breakpoint);

			g_free (breakpoint->filepath);
			g_free (breakpoint);

			g_snprintf (breakpoint_desc, MAX_FILEPATH_LENGTH, "%s:%d", filepath, line);

			debug_command_exec ("clear", breakpoint_desc, NULL);

			return;
		}
	}

	debug_breakpoint_add (breakpoint_desc);
}

gboolean
debug_is_active ()
{
	g_mutex_lock (&debug_mutex);

	gboolean ret;

	ret = debugging;
	g_mutex_unlock (&debug_mutex);

	return ret;
}

void
debug_current_file_line (const gboolean startup, gchar *filename, const gint size, gint *line)
{
	gchar *output;
	gint i;
	gint j;

	output = (gchar *) g_malloc (MAX_RESULT_LENGTH + 1);
	debug_command_exec ("info line", NULL, output);

	if (debug_conection_broken (output)) {
		filename[0] = 0;
		*line = 0;
		g_free ((gpointer) output);

		return;
	}

	i = 0;
	if (FALSE) {
		while (output[i] && !g_str_has_prefix (output + i, " at ")) {
			i++;
		}
		i += strlen (" at ");
		j = 0;
		while (j < size - 1 && output[i] && output[i] != ':') {
			filename[j] = output[i];
			i++;
			j++;
		}
		filename[j] = 0;

		sscanf (output + i + 1, "%d", line);
	}
	else {
		while (output[i] && output[i] != ' ') {
			i++;
		}
		i++;
		sscanf (output + i, "%d", line);
		while (output[i] && output[i] != '\"') {
			i++;
		}
		i++;
		j = 0;
		while (j < size - 1 && output[i] && output[i] != '\"') {
			filename[j] = output[i];
			j++;
			i++;
		}
		filename[j] = 0;
	}

	g_free ((gpointer) output);
}

void
debug_current_locals (GList **locals)
{
	gchar *output;
	gint i;
	gint j;

	output = (gchar *) g_malloc (MAX_RESULT_LENGTH + 1);
	debug_command_exec ("info locals", NULL, output);

	if (debug_conection_broken (output)) {
		*locals = NULL;

		g_free ((gpointer) output);

		return;
	}

	if (!g_str_has_prefix (output, "No locals.")) {
		gchar name[MAX_LINE_LENGTH + 1];
		gchar value[MAX_LINE_LENGTH + 1];

		i = 0;

		while (output[i] != 0) {
			gchar *local;

			local = (gchar *) g_malloc (MAX_LINE_LENGTH + 1);
			sscanf (output + i, "%s", name);
			i += strlen (name) + 3;
			j = 0;

			while (output[i + j] && output[i + j] != '\n') {
				value[j] = output[i + j];
				j++;
			}
			value[j] = 0;
			i += strlen (value) + 1;

			g_snprintf (local, MAX_LINE_LENGTH, "%s %s", name, value);

			*locals = g_list_append (*locals, (gpointer) local);
		}
	}

	g_free (output);
}

void
debug_expression_value (const gchar *expression, gchar *value, const gint size)
{
	gchar *output;
	gint i;

	output = (gchar *) g_malloc (MAX_RESULT_LENGTH + 1);
	debug_command_exec ("p", expression, output);

	if (debug_conection_broken (output)) {
		g_strlcpy (value, _("Can't get the value."), size);

		g_free (output);

		return;
	}

	i = 0;
	while (output[i] && !g_str_has_prefix (output + i, " = ")) {
		i++;
	}
	if (output[i] != 0) {
		i += 3;
		g_strlcpy (value, output + i, size);

		if (value[strlen (value) - 1] == '\n') {
			value[strlen (value) - 1] = 0;
		}
	}
	else {
		g_strlcpy (value, _("Can't get the value."), size);
	}

	g_free ((gpointer) output);
}

static void
debug_parse_gdb_bt_line (const gchar *output, const gboolean first, gchar *frame_name, gchar *frame_args,
						 gchar *file_line, gint *offset)
{
	gint i;
	gint len;
	gint line_end;

	len = strlen (output);
	frame_name[0] = 0;
	frame_args[0] = 0;
	file_line[0] = 0;

	line_end = *offset;
	while (line_end < len && output[line_end] != '\n') {
		line_end++;
	}

	while (output[*offset] && !g_str_has_prefix (output + *offset, "  ")) {
		(*offset)++;
	}
	(*offset) += 2;

	if (!first) {
		while (*offset < line_end && !g_str_has_prefix (output + *offset, " in ")) {
			(*offset)++;
		}
		(*offset) += 4;
	}

	sscanf (output + *offset, "%s", frame_name);
	(*offset) += strlen (frame_name) + 1;
	i = 0;
	while (*offset < line_end && !g_str_has_prefix (output + *offset, " at ") 
		   && !g_str_has_prefix (output + *offset, " from ")) {
		frame_args[i] = output[(*offset)];
		(*offset)++;
		i++;
	}
	frame_args[i] = 0;
	if (*offset < line_end && g_str_has_prefix (output + *offset, " at ")) {
		(*offset) += strlen (" at ");
	}
	else if (*offset < line_end && !g_str_has_prefix (output + *offset, " from ")) {
		(*offset) += strlen (" from ");
	}
	else {
		(*offset) = line_end + 1;
		return;
	}
	sscanf (output + (*offset), "%s", file_line);
	(*offset) = line_end + 1;
}

void
debug_current_stack (GList **stack)
{
	gchar *output;
	gchar frame_name[MAX_LINE_LENGTH + 1];
	gchar frame_args[MAX_LINE_LENGTH + 1];
	gchar file_line[MAX_LINE_LENGTH + 1];
	gint i;
	gchar *line;

	output = (gchar *) g_malloc (MAX_RESULT_LENGTH + 1);
	debug_command_exec ("bt", NULL, output);

	if (debug_conection_broken (output)) {
		g_free (output);
		*stack = NULL;

		return;
	}

	i = 0;
	if (output[0] != '#') {
		while (output[i] && !g_str_has_prefix (output + i, "\n#")) {
			i++;
		}
		i++;
	}

	debug_parse_gdb_bt_line (output, TRUE, frame_name, frame_args, file_line, &i);
	line = (gchar *) g_malloc (MAX_LINE_LENGTH + 1);
	g_snprintf (line, MAX_LINE_LENGTH, "%s %s %s", frame_name, file_line, frame_args);
	*stack = g_list_append (*stack, (gpointer) line);
	while (output[i]) {
		debug_parse_gdb_bt_line (output, FALSE, frame_name, frame_args, file_line, &i);
		line = (gchar *) g_malloc (MAX_LINE_LENGTH);
		g_snprintf (line, MAX_LINE_LENGTH, "%s %s %s", frame_name, file_line, frame_args);

		*stack = g_list_append (*stack, (gpointer) line);
	}

	g_free ((gpointer) output);
}

void
debug_stop ()
{
	if (!kill (target_pid, 0)) {
		kill (target_pid, SIGKILL);
	}
	if (!kill (gdb_pid, 0)) {
		kill (gdb_pid, SIGKILL);
	}
	if (!kill (proc_pid, 0)) {
		kill (proc_pid, SIGKILL);
	}

	g_mutex_lock (&debug_mutex);
	debugging = FALSE;
	g_mutex_unlock (&debug_mutex);
}

gboolean 
debug_monitor (gpointer data)
{
	if (debug_conection_broken (NULL)) {
		ui_disable_debug_widgets ();
		ui_enable_project_widgets ();
		ui_disable_debug_view ();
		ui_debug_ptr_remove ();

		debug_stop ();

		return FALSE;
	}
	
	return TRUE;
}

