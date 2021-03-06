/*
 * highlighting.c
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

#include <gtk/gtk.h>
#include "highlighting.h"
#include "keywords.h"
#include "editorconfig.h"
#include "ui.h"

#define MAX_LEX_SIZE 1000000

static gchar lex[MAX_LEX_SIZE + 1];

void
highlight_init ()
{
	keywords_init ();
}

void 
highlight_register (GtkTextBuffer *buffer)
{  
	const CEditorConfig *editor_config;

	editor_config = editorconfig_config_get ();

	tag_create_tags (buffer, editor_config);
}

void
highlight_replace (GtkTextBuffer *buffer)
{
	const CEditorConfig *editor_config;

	editor_config = editorconfig_config_get ();
	tag_replace_tags (buffer, editor_config);
}

static gboolean
highlight_is_keyword (gchar *word)
{
	/* Check whether word is a C/C++ keyword. */
	return keywords_is_keyword (word);
}

void
highlight_add_tag (GtkTextBuffer *buffer, GtkTextIter *startitr,
				   gint offset, gint len, gchar *tag)
{
	/* Apply highlighting. */
	gint start_offset;
	gint end_offset;
	GtkTextIter start, end;
	
	start_offset = gtk_text_iter_get_offset (startitr) + offset;
	end_offset = start_offset + len;
	gtk_text_buffer_get_iter_at_offset (buffer, &start, start_offset);
	gtk_text_buffer_get_iter_at_offset (buffer, &end, end_offset);
	gtk_text_buffer_apply_tag_by_name (buffer, tag, &start, &end);
}

void
highlight_apply (GtkTextBuffer *buffer, GtkTextIter *start,
				 GtkTextIter *end)
{
	/* Use a finite state machine to highlighting a code. */
	gchar *text;	
	gint i;
	gint lex_len;
	gint state;
	gint start_offset;
	
	text = gtk_text_iter_get_text (start, end);
	lex_len = 0;
	start_offset = 0;
	lex[0] = 0;
	state = 0;

	gtk_text_buffer_remove_all_tags (buffer, start, end);
	gtk_text_buffer_apply_tag_by_name (buffer, CODE_TAG_NONE, start, end);

	for (i = 0; text[i]; i++) {		
		if ((!CHAR (text[i]) && !DIGIT (text[i]) && text[i] > 0) || text[i + 1] == 0) {			
			gchar *tag;
			
			start_offset = i - lex_len;
			tag = CODE_TAG_NONE;

			switch (state) {
				case 0:
					if (lex_len > 0 && (text[i] == '\"' || text[i] == '\'' || text[i] == '/')) {
						i--;
					}
					else if (text[i] == '\"') {
						state = 1;
						lex[lex_len++] = text[i];
					}
					else if (text[i] == '\'') {
						state = 2;
						lex[lex_len++] = text[i];
					}
					else if (text[i] == '/') {
						state = 3;
						lex[lex_len++] = text[i];
					}
					else if (text[i + 1] == 0 && (CHAR (text[i]) || 
							 DIGIT (text[i]))) {
						lex[lex_len++] = text[i];
					}
					break;
					
				case 1:
					lex[lex_len++] = text[i];
					if (text[i] == '\"') {
						gint s = 0;
						gint j = lex_len - 2;
						
						for ( ; j >= 0; j--) {
							if (lex[j] == '\\') {
								s++;
							}
							else {
								break;
							}
						}
						if (s % 2 == 0) {
							state = 0;
							tag = CODE_TAG_STRING;
						}
					}
					break;
					
				case 2:
					lex[lex_len++] = text[i];
					if (text[i] == '\'') {
						gint s = 0;
						gint j = lex_len - 2;
						
						for ( ; j >= 0; j--) {
							if (lex[j] == '\\') {
								s++;
							}
							else {
								break;
							}
						}
						if (s % 2 == 0) {
							state = 0;
							tag = CODE_TAG_STRING;
						}
					}
					break;
				
				case 3:
					lex[lex_len++] = text[i];
					if (text[i] == '/') {
						state = 4;
					}
					else if (text[i] == '*') {
						state = 5;
					}
					else {
						state = 0;
						i--;
						lex_len--;
						tag = CODE_TAG_NONE;
					}
					break;
				
				case 4:
					lex[lex_len++] = text[i];
					if (text[i] == '\n' || text[i + 1] == '\0') {
						state = 0;
						tag = CODE_TAG_COMMENT;
					}
					break;
					
				case 5:
					lex[lex_len++] = text[i];
					if (text[i] == '*') {
						state = 6;
					}
					break;
					
				case 6:
					lex[lex_len++] = text[i];
					if (text[i] == '/' && text[i - 1] == '*') {
						state = 0;
						tag = CODE_TAG_COMMENT;
					}
					break;
			}
			
			lex[lex_len] = 0;
			
			if (state != 0) {
				continue;
			}
			
			if (g_strcmp0 (tag, CODE_TAG_NONE) == 0) {
				if (DIGIT (lex[0])) {
					tag = CODE_TAG_CONSTANT;
				}
				else if (lex[0] == '#') {
					tag = CODE_TAG_PREPROCESSOR;
				}
				else if (CHAR (lex[0]) && lex_len <= MAX_KEYWORD_LENGTH && highlight_is_keyword (lex)) {
					tag = CODE_TAG_KEYWORD;
				}
			}

			if (g_strcmp0 (tag, CODE_TAG_NONE) != 0) {
				glong utf8_start_offset = g_utf8_pointer_to_offset (text, text + start_offset);
				glong utf8_lex_len = g_utf8_strlen (lex, lex_len);

				highlight_add_tag (buffer, start, utf8_start_offset, utf8_lex_len, tag);
			}

			lex_len = 0;
		}
		else {
			lex[lex_len++] = text[i];
			if (state == 3) {
				state = 0;
			}
		}		
	}

	if (state == 4 && lex_len > 0) {
		glong utf8_start_offset = g_utf8_pointer_to_offset (text, text + start_offset);
		glong utf8_lex_len = g_utf8_strlen (lex, lex_len);

		highlight_add_tag (buffer, start, utf8_start_offset, utf8_lex_len, CODE_TAG_COMMENT);
	}
	
	g_free ((gpointer) text);
}

void
highlight_set_tab (GtkTextView *textview)
{
	PangoTabArray *tab_array;
	PangoLayout *layout;
	gchar *tab_string;
	gint width, height;
	const CEditorConfig *config;

	config = editorconfig_config_get ();
	tab_string = g_strnfill (4, ' ');
	layout = gtk_widget_create_pango_layout (GTK_WIDGET (textview), tab_string);
	pango_layout_set_font_description (layout, config->pfd);
	pango_layout_get_pixel_size (layout, &width, &height);
	tab_array = pango_tab_array_new (1, TRUE);
	pango_tab_array_set_tab (tab_array, 0, PANGO_TAB_LEFT, width);
	gtk_text_view_set_tabs (GTK_TEXT_VIEW (textview), tab_array);

	g_free ((gpointer) tab_string);
	pango_tab_array_free (tab_array);
	g_object_unref (G_OBJECT (layout));
}

gboolean 
highlight_parse (gpointer data)
{
	CEditor *editor;

	if (!ui_current_editor_get_need_highlight ()) {
		return TRUE;
	}

	editor = ui_get_current_editor ();
	if (editor != NULL) {
		GtkTextBuffer *buffer;
		GtkTextIter start, end;
		
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (editor->textview));
		gtk_text_buffer_get_start_iter (buffer, &start);
		gtk_text_buffer_get_end_iter (buffer, &end);

		highlight_apply (buffer, &start, &end);

		ui_current_editor_set_need_highlight (FALSE);

		return TRUE;
	}

	return TRUE;
}
