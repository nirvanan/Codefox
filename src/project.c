/*
 * project.c
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

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "project.h"
#include "misc.h"
#include "limits.h"

#define MAX_MAKEFILE_LENGTH 100000
#define DIR_MODE 0777

static gchar *default_projects_root;

static void project_save_xml(CProject *project);
static void project_load_xml(CProject *project, const gchar *xml_file);
static void project_generate_makefile(CProject *project);

static CProject *project;
static GMutex project_mutex;

/* Initialize projects root directory. */
void
project_path_init ()
{
	gboolean suc;

	default_projects_root = (gchar *) malloc(MAX_FILEPATH_LENGTH + 1);

	g_snprintf (default_projects_root, MAX_FILEPATH_LENGTH, "%s/Projects", g_getenv("HOME"));

	suc = g_mkdir (default_projects_root, DIR_MODE);
	if (suc == 0) {
		g_message ("default projects root is created.");
	}
}

static void
project_init (CProject *project)
{
	project->project_name = (gchar *) g_malloc (MAX_FILEPATH_LENGTH + 1);
	project->project_path = (gchar *) g_malloc (MAX_FILEPATH_LENGTH + 1);
	project->header_list = NULL;
	project->source_list = NULL;
	project->resource_list = NULL;
	project->libs = (gchar *) g_malloc (MAX_OPTION_LENGTH + 1);
	project->libs[0] = 0;
	project->opts = (gchar *) g_malloc (MAX_OPTION_LENGTH + 1);
	project->opts[0] = 0;
}

/* Create a new project. */
CProject *
project_new(const gchar *project_name, const gchar *project_dir, const CProjectType project_type)
{
	CProject *new_project;
	gboolean suc;

	new_project = (CProject *) g_malloc (sizeof (CProject));
	project_init (new_project);
	g_strlcpy (new_project->project_name, project_name, MAX_FILEPATH_LENGTH);
	g_snprintf (new_project->project_path, MAX_FILEPATH_LENGTH, "%s/%s",
				project_dir == NULL? default_projects_root: project_dir,
				project_name);
	new_project->project_type = project_type;

	suc = g_mkdir (new_project->project_path, DIR_MODE);
	if (suc == -1) {
		g_error ("failed to create project directory %s.", new_project->project_path);

		g_free ((gpointer) new_project);
	}

	project_save_xml(new_project);
	project = new_project;
	project_generate_makefile (project);

	return new_project;
}

/* Create a new project from exists. */
CProject *
project_new_from_xml(const gchar *xml_file)
{
	CProject *new_project;

	new_project = (CProject *) g_malloc (sizeof (CProject));
	project_init (new_project);
	project_load_xml(new_project, xml_file);

	project = new_project;

	return new_project;
}

static void
project_save_xml(CProject *project)
{	
	xmlDocPtr doc;
	xmlNodePtr root_node;
	xmlNodePtr node;
	GList *iterator;
	gchar xml_path[MAX_FILEPATH_LENGTH + 1];

	doc = xmlNewDoc(BAD_CAST ("1.0"));
	root_node = xmlNewNode(NULL, BAD_CAST ("Project"));
	xmlDocSetRootElement(doc, root_node);
	xmlNewChild(root_node, NULL, BAD_CAST ("Type"), project->project_type == PROJECT_C? BAD_CAST ("C"): BAD_CAST ("C++"));
	xmlNewChild(root_node, NULL, BAD_CAST ("Name"), BAD_CAST (project->project_name));
	xmlNewChild(root_node, NULL, BAD_CAST ("Path"), BAD_CAST (project->project_path));

	node = xmlNewChild(root_node, NULL, BAD_CAST ("Headers"), NULL);
	for (iterator = project->header_list; iterator; iterator = iterator->next) {
		gchar *filepath;

		filepath = (gchar *) iterator->data;
		xmlNewChild(node, NULL, BAD_CAST ("File"), BAD_CAST (filepath));
	}

	node = xmlNewChild(root_node, NULL, BAD_CAST ("Sources"), NULL);
	for (iterator = project->source_list; iterator; iterator = iterator->next) {
		gchar *filepath;

		filepath = (gchar *) iterator->data;
		xmlNewChild(node, NULL, BAD_CAST ("File"), BAD_CAST (filepath));
	}

	node = xmlNewChild(root_node, NULL, BAD_CAST ("Resources"), NULL);
	for (iterator = project->resource_list; iterator; iterator = iterator->next) {
		gchar *filepath;

		filepath = (gchar *) iterator->data;
		xmlNewChild(node, NULL, BAD_CAST ("File"), BAD_CAST (filepath));
	}

	xmlNewChild(root_node, NULL, BAD_CAST ("LIBS"), BAD_CAST (project->libs));
	xmlNewChild(root_node, NULL, BAD_CAST ("OPTS"), BAD_CAST (project->opts));

	g_strlcpy (xml_path, project->project_path, MAX_FILEPATH_LENGTH);
	g_strlcat (xml_path, "/project.cfp", MAX_FILEPATH_LENGTH);
	xmlSaveFormatFileEnc(xml_path, doc, "UTF-8", 1);

	xmlFreeDoc(doc);
	xmlCleanupParser();
}

static void 
project_load_xml(CProject *project, const gchar *xml_file)
{
	xmlDocPtr doc;
	xmlNodePtr root_node;
	xmlNodePtr child;
	xmlNodePtr node;
	xmlChar *tmp_c;
	gchar type[MAX_FILEPATH_LENGTH + 1];

	doc = xmlReadFile(xml_file, "UTF-8", 0);
	root_node = xmlDocGetRootElement(doc);
	child = root_node->children;
	while (child->type != XML_ELEMENT_NODE) {
		child = child->next;
	}
	tmp_c = xmlNodeGetContent(child);
	g_strlcpy (type, (const gchar*) tmp_c, MAX_FILEPATH_LENGTH);
	xmlFree (tmp_c);
	project->project_type = (g_strcmp0(type, "C++") == 0);

	child = child->next;
	while (child->type != XML_ELEMENT_NODE) {
		child = child->next;
	}
	tmp_c = xmlNodeGetContent(child);
	g_strlcpy (project->project_name, (const gchar*) tmp_c, MAX_FILEPATH_LENGTH);
	xmlFree (tmp_c);

	child = child->next;
	while (child->type != XML_ELEMENT_NODE) {
		child = child->next;
	}
	tmp_c = xmlNodeGetContent(child);
	g_strlcpy (project->project_path, (const gchar*) tmp_c, MAX_FILEPATH_LENGTH);
	xmlFree (tmp_c);

	child = child->next;
	while (child->type != XML_ELEMENT_NODE) {
		child = child->next;
	}
	for (node = child->children; node; node = node->next) {
		gchar *new_header;

		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}
		new_header = (gchar *) g_malloc (MAX_FILEPATH_LENGTH + 1);
		tmp_c = xmlNodeGetContent(node);
		g_strlcpy (new_header, (const gchar*) tmp_c, MAX_FILEPATH_LENGTH);
		project->header_list = g_list_append (project->header_list, (gpointer) new_header);
		xmlFree (tmp_c);
	}

	child = child->next;
	while (child->type != XML_ELEMENT_NODE) {
		child = child->next;
	}
	for (node = child->children; node; node = node->next) {
		gchar *new_source;

		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}
		new_source = (gchar *) g_malloc (MAX_FILEPATH_LENGTH + 1);
		tmp_c = xmlNodeGetContent(node);
		g_strlcpy (new_source, (const gchar*) tmp_c, MAX_FILEPATH_LENGTH);
		project->source_list = g_list_append (project->source_list, (gpointer) new_source);
		xmlFree (tmp_c);
	}

	child = child->next;
	while (child->type != XML_ELEMENT_NODE) {
		child = child->next;
	}
	for (node = child->children; node; node = node->next) {
		gchar *new_resource;

		if (node->type != XML_ELEMENT_NODE) {
			continue;
		}
		new_resource = (gchar *) g_malloc (MAX_FILEPATH_LENGTH + 1);
		tmp_c = xmlNodeGetContent(node);
		g_strlcpy (new_resource, (const gchar*) tmp_c, MAX_FILEPATH_LENGTH);
		project->resource_list = g_list_append (project->resource_list, (gpointer) new_resource);
		xmlFree (tmp_c);
	}

	child = child->next;
	while (child->type != XML_ELEMENT_NODE) {
		child = child->next;
	}
	tmp_c = xmlNodeGetContent(child);
	g_strlcpy (project->libs, (const gchar*) tmp_c, MAX_FILEPATH_LENGTH);
	xmlFree (tmp_c);

	child = child->next;
	while (child->type != XML_ELEMENT_NODE) {
		child = child->next;
	}
	tmp_c = xmlNodeGetContent(child);
	g_strlcpy (project->opts, (const gchar*) tmp_c, MAX_FILEPATH_LENGTH);
	xmlFree (tmp_c);

	xmlFreeDoc (doc);
}

void
project_get_default_path (gchar *buf, const gint size)
{
	g_strlcpy (buf, default_projects_root, size);
}

gboolean
project_create_empty (const gchar *filepath, const gchar *filename, const gint file_type)
{
	gchar *final_path;
	gboolean suc;

	final_path = (gchar *) g_malloc (MAX_FILEPATH_LENGTH + 1);

	g_snprintf (final_path, MAX_FILEPATH_LENGTH, "%s/%s", filepath, filename);

	suc = misc_create_file (final_path);
	if (suc) {
		switch (file_type) {
		case FILE_HEADER:
			project->header_list = g_list_append (project->header_list, (gpointer) final_path);
			break;
		case FILE_SOURCE:
			project->source_list = g_list_append (project->source_list, (gpointer) final_path);
			break;
		case FILE_RESOURCE:
			project->resource_list = g_list_append (project->resource_list, (gpointer) final_path);
			break;
		}
		project_save_xml (project);
	}

	return suc;
}

gboolean
project_add_file(const gchar *filepath, const gchar *filename, const gchar *local_file, const gint file_type)
{
	gchar *final_path;
	gboolean suc;

	final_path = (gchar *) g_malloc (MAX_FILEPATH_LENGTH + 1);

	g_snprintf (final_path, MAX_FILEPATH_LENGTH, "%s/%s", filepath, filename);

	suc = misc_copy_file (local_file, final_path);
	if (suc) {
		switch (file_type) {
		case FILE_HEADER:
			project->header_list = g_list_append (project->header_list, (gpointer) final_path);
			break;
		case FILE_SOURCE:
			project->source_list = g_list_append (project->source_list, (gpointer) final_path);
			break;
		case FILE_RESOURCE:
			project->resource_list = g_list_append (project->resource_list, (gpointer) final_path);
			break;
		}
		project_save_xml (project);
	}

	return suc;
}

static gboolean
project_delete_file_from_list(GList **list, const gchar *filepath)
{
	GList *iterator;

	for (iterator = *list; iterator; iterator = iterator->next) {
		gchar *path;

		path = (gchar *) iterator->data;
		if (g_strcmp0 (filepath, path) == 0) {
			*list = g_list_remove (*list, path);

			g_free ((gpointer) path);

			return TRUE;
		}
	}

	return FALSE;

}

gboolean
project_delete_file(const gchar *filepath, const gint file_type)
{
	gboolean suc;

	suc = misc_delete_file (filepath);

	if (!suc) {
		return suc;
	}

	switch (file_type) {
	case FILE_HEADER:
		suc = project_delete_file_from_list (&(project->header_list), filepath);
		break;
	case FILE_SOURCE:
		suc = project_delete_file_from_list (&(project->source_list), filepath);
		break;
	case FILE_RESOURCE:
		suc = project_delete_file_from_list (&(project->resource_list), filepath);
		break;
	}
	
	if (suc) {
		project_save_xml (project);
	}

	return suc;
}

static void
project_generate_makefile(CProject *project)
{
	gchar makefile_path[MAX_FILEPATH_LENGTH + 1];
	gchar makefile_buf[MAX_FILEPATH_LENGTH + 1];

	g_snprintf (makefile_path, MAX_FILEPATH_LENGTH, "%s/Makefile", project->project_path);

	if (project->project_type == PROJECT_C) {
		g_strlcpy (makefile_buf, "CC=gcc\n", MAX_MAKEFILE_LENGTH);
	}
	else {
		g_strlcpy (makefile_buf, "CC=g++\n", MAX_MAKEFILE_LENGTH);
	}

	g_strlcat (makefile_buf, "PROG_NAME=", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, project->project_name, MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "\nINCS=$(wildcard *.h)\n", MAX_MAKEFILE_LENGTH);

	if (project->project_type == PROJECT_C) {
		g_strlcat (makefile_buf, "SRCS=$(wildcard *.c)\n", MAX_MAKEFILE_LENGTH);
	}
	else {
		g_strlcat (makefile_buf, "SRCS=$(wildcard *.cpp *.cxx *.C *.cc *.c++)\n", MAX_MAKEFILE_LENGTH);
	}

	g_strlcat (makefile_buf, "DEFAULT_OPTS=-g -Wall\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "OPTS=", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, project->opts, MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "\nOBJS=$(patsubst %c, %o, $(SRCS))\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "LIBS=", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, project->libs, MAX_MAKEFILE_LENGTH);
	if (strlen (project->libs) > 0) {
		g_strlcat (makefile_buf, "\nCFLAGS=`pkg-config --cflags ${LIBS}` $(DEFAULT_OPTS) $(OPTS)\n", MAX_MAKEFILE_LENGTH);
		g_strlcat (makefile_buf, "LDFLAGS=`pkg-config --libs ${LIBS}` $(DEFAULT_OPTS) $(OPTS)\n\n", MAX_MAKEFILE_LENGTH);
	}
	else {
		g_strlcat (makefile_buf, "\nCFLAGS=$(DEFAULT_OPTS) $(OPTS)\n", MAX_MAKEFILE_LENGTH);
		g_strlcat (makefile_buf, "LDFLAGS=$(DEFAULT_OPTS) $(OPTS)\n\n", MAX_MAKEFILE_LENGTH);
	}
	g_strlcat (makefile_buf, "all: ${PROG_NAME}\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "${PROG_NAME}:${OBJS}\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "\t${CC} -o ${PROG_NAME} ${OBJS} ${LDFLAGS}\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "${OBJS}:${INCS}\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, ".c.o:\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "\t${CC} -c $<   ${CFLAGS}\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "clean:\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "\trm -f *.o   ${PROG_NAME}\n", MAX_MAKEFILE_LENGTH);
	g_strlcat (makefile_buf, "rebuild: clean all\n", MAX_MAKEFILE_LENGTH);

	misc_set_file_content (makefile_path, makefile_buf);
}

gchar *
project_current_path()
{
	gchar *ret;

	if (project == NULL) {
		ret = NULL;
	}
	else {
		ret = project->project_path;
	}
	
	return ret;
}

gchar *
project_current_name()
{
	if (project == NULL) {
		return NULL;
	}

	return project->project_name;
}

void
project_get_file_lists(GList **header_list, GList **source_list, GList **resource_list)
{
	if (project == NULL) {
		(*header_list) = NULL;
		(*source_list) = NULL;
		(*resource_list) = NULL;
	}
	else {
		(*header_list) = project->header_list;
		(*source_list) = project->source_list;
		(*resource_list) = project->resource_list;
	}
}

void
project_get_settings(gchar *libs, const gint libs_size, gchar *opts, const gint opts_size)
{
	g_mutex_lock (&project_mutex);

	if (project == NULL) {
		libs[0] = 0;
		opts[0] = 0;
	}
	else {
		if (libs != NULL) {
			g_strlcpy (libs, project->libs, libs_size);
		}
		if (opts != NULL) {
			g_strlcpy (opts, project->opts, opts_size);
		}
	}

	g_mutex_unlock (&project_mutex);
}

void
project_set_settings(const gchar *libs, const gchar *opts)
{
	g_mutex_lock (&project_mutex);
	if (project == NULL) {
		return;
	}

	g_strlcpy (project->libs, libs, MAX_FILEPATH_LENGTH);
	g_strlcpy (project->opts, opts, MAX_FILEPATH_LENGTH);

	project_save_xml (project);
	project_generate_makefile (project);

	g_mutex_unlock (&project_mutex);
}

gint
project_get_type()
{
	return project->project_type;
}

void
project_mutex_init ()
{
	g_mutex_init (&project_mutex);
}
