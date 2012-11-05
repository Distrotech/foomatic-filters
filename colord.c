/* colord.c
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
 *
 * This file is part of foomatic-rip.
 *
 * Foomatic-rip is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Foomatic-rip is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* Common routines for accessing the colord CMS framework */

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "colord.h"

#define QUAL_COLORSPACE   0
#define QUAL_MEDIA        1
#define QUAL_RESOLUTION   2
#define QUAL_SIZE         3

static char *
get_filename_for_profile_path (DBusConnection *con,
                               const char *object_path)
{
  char *filename = NULL;
  const char *interface = "org.freedesktop.ColorManager.Profile";
  const char *property = "Filename";
  const char *tmp;
  DBusError error;
  DBusMessageIter args;
  DBusMessage *message = NULL;
  DBusMessage *reply = NULL;
  DBusMessageIter sub;

  message = dbus_message_new_method_call("org.freedesktop.ColorManager",
                 object_path,
                 "org.freedesktop.DBus.Properties",
                 "Get");

  dbus_message_iter_init_append(message, &args);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &property);

  /* send syncronous */
  dbus_error_init(&error);
  fprintf(stderr, "DEBUG: Calling %s.Get(%s)\n", interface, property);
  reply = dbus_connection_send_with_reply_and_block(con,
                message,
                -1,
                &error);
  if (reply == NULL) {
    fprintf(stderr, "DEBUG: Failed to send: %s:%s\n",
           error.name, error.message);
    dbus_error_free(&error);
    goto out;
  }

  /* get reply data */
  dbus_message_iter_init(reply, &args);
  if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT) {
    fprintf(stderr, "DEBUG: Incorrect reply type\n");
    goto out;
  }

  dbus_message_iter_recurse(&args, &sub);
  dbus_message_iter_get_basic(&sub, &tmp);
  filename = strdup(tmp);
out:
  if (message != NULL)
    dbus_message_unref(message);
  if (reply != NULL)
    dbus_message_unref(reply);
  return filename;
}

static char *
get_profile_for_device_path (DBusConnection *con,
                             const char *object_path,
                             const char **split)
{
  char **key = NULL;
  char *profile = NULL;
  char str[256];
  const char *tmp;
  DBusError error;
  DBusMessageIter args;
  DBusMessageIter entry;
  DBusMessage *message = NULL;
  DBusMessage *reply = NULL;
  int i = 0;
  const int max_keys = 7;

  message = dbus_message_new_method_call("org.freedesktop.ColorManager",
                                         object_path,
                                         "org.freedesktop.ColorManager.Device",
                                         "GetProfileForQualifiers");
  dbus_message_iter_init_append(message, &args);

  /* create the fallbacks */
  key = calloc(max_keys + 1, sizeof(char*));

  /* exact match */
  i = 0;
  snprintf(str, sizeof(str), "%s.%s.%s",
           split[QUAL_COLORSPACE],
           split[QUAL_MEDIA],
           split[QUAL_RESOLUTION]);
  key[i++] = strdup(str);
  snprintf(str, sizeof(str), "%s.%s.*",
           split[QUAL_COLORSPACE],
           split[QUAL_MEDIA]);
  key[i++] = strdup(str);
  snprintf(str, sizeof(str), "%s.*.%s",
           split[QUAL_COLORSPACE],
           split[QUAL_RESOLUTION]);
  key[i++] = strdup(str);
  snprintf(str, sizeof(str), "%s.*.*",
           split[QUAL_COLORSPACE]);
  key[i++] = strdup(str);
  key[i++] = strdup("*");
  dbus_message_iter_open_container(&args,
                                   DBUS_TYPE_ARRAY,
                                   "s",
                                   &entry);
  for (i=0; key[i] != NULL; i++) {
    dbus_message_iter_append_basic(&entry,
                                   DBUS_TYPE_STRING,
                                   &key[i]);
  }
  dbus_message_iter_close_container(&args, &entry);

  /* send syncronous */
  dbus_error_init(&error);
  fprintf(stderr, "DEBUG: Calling GetProfileForQualifiers(%s...)\n", key[0]);
  reply = dbus_connection_send_with_reply_and_block(con,
                                                    message,
                                                    -1,
                                                    &error);
  if (reply == NULL) {
    fprintf(stderr, "DEBUG: Failed to send: %s:%s\n",
           error.name, error.message);
    dbus_error_free(&error);
    goto out;
  }

  /* get reply data */
  dbus_message_iter_init(reply, &args);
  if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH) {
    fprintf(stderr, "DEBUG: Incorrect reply type\n");
    goto out;
  }
  dbus_message_iter_get_basic(&args, &tmp);
  fprintf(stderr, "DEBUG: Found profile %s\n", tmp);

  /* get filename */
  profile = get_filename_for_profile_path(con, tmp);

out:
  if (message != NULL)
    dbus_message_unref(message);
  if (reply != NULL)
    dbus_message_unref(reply);
  if (key != NULL) {
    for (i=0; i < max_keys; i++)
      free(key[i]);
    free(key);
  }
  return profile;
}

static char *
get_profile_for_device_id (DBusConnection *con,
                           const char *device_id,
                           const char **qualifier_tuple)
{
  char *profile = NULL;
  const char *device_path_tmp;
  DBusError error;
  DBusMessageIter args;
  DBusMessage *message = NULL;
  DBusMessage *reply = NULL;

  message = dbus_message_new_method_call("org.freedesktop.ColorManager",
                                         "/org/freedesktop/ColorManager",
                                         "org.freedesktop.ColorManager",
                                         "FindDeviceById");
  dbus_message_iter_init_append(message, &args);
  dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &device_id);

  /* send syncronous */
  dbus_error_init(&error);
  fprintf(stderr, "DEBUG: Calling FindDeviceById(%s)\n", device_id);
  reply = dbus_connection_send_with_reply_and_block(con,
                message,
                -1,
                &error);
  if (reply == NULL) {
    fprintf(stderr, "DEBUG: Failed to send: %s:%s\n",
            error.name, error.message);
    dbus_error_free(&error);
    goto out;
  }

  /* get reply data */
  dbus_message_iter_init(reply, &args);
  if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_OBJECT_PATH) {
    fprintf(stderr, "DEBUG: Incorrect reply type\n");
    goto out;
  }
  dbus_message_iter_get_basic(&args, &device_path_tmp);
  fprintf(stderr, "DEBUG: Found device %s\n", device_path_tmp);
  profile = get_profile_for_device_path(con, device_path_tmp, qualifier_tuple);
out:
  if (message != NULL)
    dbus_message_unref(message);
  if (reply != NULL)
    dbus_message_unref(reply);
  return profile;
}

char *
colord_get_profile_for_device_id (const char *device_id,
                              const char **qualifier_tuple)
{
  DBusConnection *con;
  char *filename = NULL;

  /* connect to system bus */
  con = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
  if (con == NULL) {
    fprintf(stderr, "ERROR: Failed to connect to system bus\n");
    goto out;
  }

  /* get the best profile for the device */
  filename = get_profile_for_device_id (con, device_id, qualifier_tuple);
  if (filename == NULL) {
    fprintf(stderr, "DEBUG: Failed to get profile filename!\n");
    goto out;
  }
  fprintf(stderr, "DEBUG: Use profile filename: '%s'\n", filename);
out:
  if (con != NULL)
    dbus_connection_unref(con);
  return filename;
}
