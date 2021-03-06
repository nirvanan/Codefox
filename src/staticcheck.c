/*
 * staticcheck.c
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

#include "staticcheck.h"
#include "compile.h"
#include "highlighting.h"
#include "misc.h"
#include "ui.h"
#include "project.h"
#include "env.h"
#include "limits.h"

extern CWindow *window;

gboolean 
static_check (gpointer data)
{	
	/* Get current code and check for errors and warnings. */
	gchar *output;
	gchar line[MAX_FILEPATH_LENGTH + 1];
	gchar *code;
	gchar *project_path;
	gchar file_path[MAX_FILEPATH_LENGTH + 1];
	gchar code_path[MAX_FILEPATH_LENGTH + 1];
	gint project_type;
	gchar libs[MAX_LINE_LENGTH + 1];
	gint p;
	gboolean error;
	gboolean warning;

	project_path = NULL;

	if (!ui_have_editor ()) {
		return TRUE;
	}

	ui_current_editor_filepath (file_path);
	if (project_path == NULL) {
		project_path = project_current_path ();
	}

	if (project_path == NULL || file_path[0] == 0) {
		return TRUE;
	}
	if (project_get_type () == PROJECT_C && !env_prog_exist (ENV_PROG_GCC)) {
		g_warning ("gcc not found.");

		return FALSE;
	}
	if (project_get_type () == PROJECT_CPP && !env_prog_exist (ENV_PROG_GPP)) {
		g_warning ("g++ not found.");

		return FALSE;
	}

	project_type = project_get_type ();
	project_get_settings (libs, MAX_LINE_LENGTH, NULL, 0);
	g_snprintf (code_path, MAX_LINE_LENGTH, "%s/.static.%s",
				project_path, project_type? "cpp": "c");

	code = ui_current_editor_code ();
	if (code == NULL) {
		return TRUE;
	}

	misc_set_file_content (code_path, code);

	g_free ((gpointer) code);

	output = (gchar *) g_malloc (MAX_RESULT_LENGTH + 1);
	compile_static_check (code_path, project_type, libs, output);

	p = 0;
	ui_current_editor_error_tag_clear ();

	error = FALSE;
	warning = FALSE;
	while (output[p]) {
		gint i = 0;
		
		while (output[p] && output[p] != '\n') {
			line[i++] = output[p++];
		}
		line[i] = 0;

		error = compile_is_error (line) || error;
		warning = compile_is_warning (line) || warning;
		if (error || warning) {
			gint row;
			gint column;
			gchar code_line[MAX_LINE_LENGTH + 1];
			gint len;
			gint code_len;

			compile_get_location (line, &row, &column);

			if (!ui_find_editor (file_path)) {
				continue;
			}

			ui_current_editor_line (code_line, MAX_LINE_LENGTH, row - 1);
			len = 0;
			code_len = strlen(code_line);
			while (column - 1 + len >= 0 && column - 1 + len < code_len 
				   && code_line[column - 1 + len] != '.') {
				len++;
			}

			if (column - 1 >= 0 && column - 1 < code_len && BRACKET (code_line[column - 1])) {
				len = 1;
			}
			
			ui_current_editor_error_tag_add (row - 1, column - 1, len);
		}
		
		p++;
	}

	ui_status_image_set (error, warning);

	g_free ((gpointer) output);

	return TRUE;
}
