/*
 * rb-audioscrobbler-account.c
 *
 * Copyright (C) 2010 Jamie Nicol <jamie@thenicols.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#include <string.h>

#include <gconf/gconf.h>

#include <libsoup/soup.h>
#include <libsoup/soup-gnome.h>

#include "eel-gconf-extensions.h"
#include "rb-audioscrobbler-account.h"
#include "rb-builder-helpers.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-util.h"

#define LASTFM_API_KEY "0337ff3c59299b6a31d75164041860b6"
#define LASTFM_API_SECRET "776c85a04a445efa8f9ed7705473c606"
#define LASTFM_API_URL "http://ws.audioscrobbler.com/2.0/"
#define LASTFM_AUTH_URL "http://www.last.fm/api/auth/"

#define LASTFM_SESSION_KEY_FILE "session_key"
#define SESSION_KEY_REQUEST_TIMEOUT 15

struct _RBAudioscrobblerAccountPrivate
{
	RBShell *shell;

	/* Authentication info */
	gchar *username;
	gchar *auth_token;
	gchar *session_key;

	/* Widgets for the prefs pane */
	GtkWidget *config_widget;
	GtkWidget *username_entry;
	GtkWidget *username_label;
	GtkWidget *auth_button;

	/* Preference notifications */
	guint notification_username_id;

	/* Timeout notifications */
	guint session_key_timeout_id;

	/* HTTP requests session */
	SoupSession *soup_session;
};

#define RB_AUDIOSCROBBLER_ACCOUNT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER_ACCOUNT, RBAudioscrobblerAccountPrivate))

static void	     rb_audioscrobbler_account_get_property (GObject *object,
                                                             guint prop_id,
                                                             GValue *value,
                                                             GParamSpec *pspec);
static void	     rb_audioscrobbler_account_set_property (GObject *object,
                                                             guint prop_id,
                                                             const GValue *value,
                                                             GParamSpec *pspec);
static void          rb_audioscrobbler_account_dispose (GObject *object);
static void          rb_audioscrobbler_account_finalize (GObject *object);

static void          rb_audioscrobbler_account_import_settings (RBAudioscrobblerAccount *account);
static void          rb_audioscrobbler_account_load_session_key (RBAudioscrobblerAccount *account);
static void          rb_audioscrobbler_account_save_session_key (RBAudioscrobblerAccount *account);
static void          rb_audioscrobbler_account_preferences_sync (RBAudioscrobblerAccount *account);

static void          rb_audioscrobbler_account_gconf_changed_cb (GConfClient *client,
                                                                 guint cnxn_id,
                                                                 GConfEntry *entry,
                                                                 RBAudioscrobblerAccount *account);

static void          rb_audioscrobbler_account_got_token_cb (SoupSession *session,
                                                             SoupMessage *msg,
                                                             gpointer user_data);
static void          rb_audioscrobbler_account_got_session_key_cb (SoupSession *session,
                                                                   SoupMessage *msg,
                                                                   gpointer user_data);

static gboolean      rb_audioscrobbler_account_request_session_key_timeout_cb (gpointer user_data);

enum
{
	PROP_0,
	PROP_SHELL,
};

G_DEFINE_TYPE (RBAudioscrobblerAccount, rb_audioscrobbler_account, G_TYPE_OBJECT)

static void
rb_audioscrobbler_account_constructed (GObject *object)
{
	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_account_parent_class, constructed, object);
}

static void
rb_audioscrobbler_account_class_init (RBAudioscrobblerAccountClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = rb_audioscrobbler_account_constructed;
	object_class->dispose = rb_audioscrobbler_account_dispose;
	object_class->finalize = rb_audioscrobbler_account_finalize;

	object_class->get_property = rb_audioscrobbler_account_get_property;
	object_class->set_property = rb_audioscrobbler_account_set_property;

	g_object_class_install_property (object_class,
	                                 PROP_SHELL,
	                                 g_param_spec_object ("shell",
	                                                      "RBShell",
	                                                      "RBShell object",
	                                                      RB_TYPE_SHELL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerAccountPrivate));
}

static void
rb_audioscrobbler_account_init (RBAudioscrobblerAccount *account)
{
	account->priv = RB_AUDIOSCROBBLER_ACCOUNT_GET_PRIVATE (account);

	account->priv->username = NULL;
	account->priv->auth_token = NULL;
	account->priv->session_key = NULL;

	rb_audioscrobbler_account_import_settings (account);
	if (account->priv->username != NULL)
		rb_audioscrobbler_account_load_session_key (account);

	account->priv->notification_username_id =
		eel_gconf_notification_add (CONF_AUDIOSCROBBLER_USERNAME,
		                            (GConfClientNotifyFunc) rb_audioscrobbler_account_gconf_changed_cb,
		                            account);

	account->priv->session_key_timeout_id = 0;

	rb_audioscrobbler_account_preferences_sync (account);
}

static void
rb_audioscrobbler_account_dispose (GObject *object)
{
	RBAudioscrobblerAccount *account;

	account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	if (account->priv->notification_username_id != 0) {
		eel_gconf_notification_remove (account->priv->notification_username_id);
		account->priv->notification_username_id = 0;
	}

	if (account->priv->session_key_timeout_id != 0) {
		g_source_remove (account->priv->session_key_timeout_id);
		account->priv->session_key_timeout_id = 0;
	}

	if (account->priv->soup_session != NULL) {
		soup_session_abort (account->priv->soup_session);
		g_object_unref (account->priv->soup_session);
		account->priv->soup_session = NULL;
	}

	G_OBJECT_CLASS (rb_audioscrobbler_account_parent_class)->dispose (object);
}

static void
rb_audioscrobbler_account_finalize (GObject *object)
{
	RBAudioscrobblerAccount *account;

	account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	g_free (account->priv->username);
	g_free (account->priv->auth_token);
	g_free (account->priv->session_key);

	G_OBJECT_CLASS (rb_audioscrobbler_account_parent_class)->finalize (object);
}

RBAudioscrobblerAccount *
rb_audioscrobbler_account_new (RBShell *shell)
{
	return g_object_new (RB_TYPE_AUDIOSCROBBLER_ACCOUNT,
	                     "shell", shell,
                             NULL);
}

static void
rb_audioscrobbler_account_get_property (GObject *object,
                                        guint prop_id,
                                        GValue *value,
                                        GParamSpec *pspec)
{
	RBAudioscrobblerAccount *account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	switch (prop_id) {
	case PROP_SHELL:
		g_value_set_object (value, account->priv->shell);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_account_set_property (GObject *object,
                                        guint prop_id,
                                        const GValue *value,
                                        GParamSpec *pspec)
{
	RBAudioscrobblerAccount *account = RB_AUDIOSCROBBLER_ACCOUNT (object);

	switch (prop_id) {
	case PROP_SHELL:
		account->priv->shell = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gchar *
mkmd5 (char *string)
{
	GChecksum *checksum;
	gchar *md5_result;

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *)string, -1);

	md5_result = g_strdup (g_checksum_get_string (checksum));
	g_checksum_free (checksum);

	return md5_result;
}

static void
rb_audioscrobbler_account_import_settings (RBAudioscrobblerAccount *account)
{
	/* import gconf settings. */
	g_free (account->priv->username);
	account->priv->username = eel_gconf_get_string (CONF_AUDIOSCROBBLER_USERNAME);
}

static void
rb_audioscrobbler_account_load_session_key (RBAudioscrobblerAccount *account)
{
	/* Attempt to load the saved session key if one exists */
	const char *rb_data_dir;
	char *file_path;
	GFile *file;
	GInputStream *stream;
	GDataInputStream *data_stream;

	rb_data_dir = rb_user_data_dir ();
	if (rb_data_dir != NULL) {
		file_path = g_build_filename (rb_data_dir, LASTFM_SESSION_KEY_FILE, NULL);
		file = g_file_new_for_path (file_path);
		stream = G_INPUT_STREAM (g_file_read (file, NULL, NULL));

		if (stream != NULL) {
			rb_debug ("loading session key from %s", file_path);
			data_stream = g_data_input_stream_new (stream);
			account->priv->session_key =
				g_data_input_stream_read_line (data_stream, NULL, NULL, NULL);
			g_object_unref (data_stream);
			g_object_unref (stream);
		}
		g_object_unref (file);
		g_free (file_path);
	}
}

static void
rb_audioscrobbler_account_save_session_key (RBAudioscrobblerAccount *account)
{
	/* Save the current session key to a file */
	const char *rb_data_dir;
	char *file_path;
	GFile *file;
	GOutputStream *stream;
	GDataOutputStream *data_stream;

	g_assert (account->priv->session_key != NULL);

	rb_data_dir = rb_user_data_dir ();
	if (rb_data_dir == NULL)
		return;

	file_path = g_build_filename (rb_data_dir, LASTFM_SESSION_KEY_FILE, NULL);
	file = g_file_new_for_path (file_path);
	stream = G_OUTPUT_STREAM (g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL));

	if (stream != NULL) {
		rb_debug ("saving session key to %s", file_path);
		data_stream = g_data_output_stream_new (stream);
		g_data_output_stream_put_string (data_stream,
		                                 account->priv->session_key,
		                                 NULL,
		                                 NULL);
		g_object_unref (data_stream);
		g_object_unref (stream);
	}
	g_object_unref (file);
	g_free (file_path);
}

static void
rb_audioscrobbler_account_preferences_sync (RBAudioscrobblerAccount *account)
{
	char *v;

	if (account->priv->config_widget == NULL)
		return;

	rb_debug ("Syncing data with preferences window");

	v = account->priv->username;
	gtk_entry_set_text (GTK_ENTRY (account->priv->username_entry),
	                    v ? v : "");
}

GtkWidget *
rb_audioscrobbler_account_get_config_widget (RBAudioscrobblerAccount *account,
                                             RBPlugin *plugin)
{
	GtkBuilder *builder;
	char *builder_file;

	if (account->priv->config_widget)
		return account->priv->config_widget;

	builder_file = rb_plugin_find_file (plugin, "audioscrobbler-prefs.ui");
	g_assert (builder_file != NULL);
	builder = rb_builder_load (builder_file, account);
	g_free (builder_file);

	account->priv->config_widget = GTK_WIDGET (gtk_builder_get_object (builder, "audioscrobbler_vbox"));
	account->priv->username_entry = GTK_WIDGET (gtk_builder_get_object (builder, "username_entry"));
	account->priv->username_label = GTK_WIDGET (gtk_builder_get_object (builder, "username_label"));
	account->priv->auth_button = GTK_WIDGET (gtk_builder_get_object (builder, "auth_button"));

	rb_builder_boldify_label (builder, "audioscrobbler_label");

	rb_audioscrobbler_account_preferences_sync (account);

	return account->priv->config_widget;
}

/* GConf callbacks */
static void
rb_audioscrobbler_account_gconf_changed_cb (GConfClient *client,
                                            guint cnxn_id,
                                            GConfEntry *entry,
                                            RBAudioscrobblerAccount *account)
{
	rb_debug ("GConf key updated: \"%s\"", entry->key);
	if (strcmp (entry->key, CONF_AUDIOSCROBBLER_USERNAME) == 0) {
		const char *username;

		username = gconf_value_get_string (entry->value);
		if (rb_safe_strcmp (username, account->priv->username) == 0) {
			rb_debug ("username not modified");
			return;
		}

		g_free (account->priv->username);
		account->priv->username = NULL;

		if (username != NULL) {
			account->priv->username = g_strdup (username);
		}

		if (account->priv->username_entry) {
			char *v = account->priv->username;
			gtk_entry_set_text (GTK_ENTRY (account->priv->username_entry),
					    v ? v : "");
		}
	} else {
		rb_debug ("Unhandled GConf key updated: \"%s\"", entry->key);
	}
}

/* UI callbacks */
void
rb_audioscrobbler_account_username_entry_focus_out_event_cb (GtkWidget *widget,
                                                             RBAudioscrobblerAccount *account)
{
	eel_gconf_set_string (CONF_AUDIOSCROBBLER_USERNAME,
                              gtk_entry_get_text (GTK_ENTRY (widget)));
}

void
rb_audioscrobbler_account_username_entry_activate_cb (GtkEntry *entry,
                                                      RBAudioscrobblerAccount *account)
{
	gtk_widget_grab_focus (account->priv->auth_button);
}

void
rb_audioscrobbler_account_auth_button_clicked_cb (GtkButton *button,
                                                  RBAudioscrobblerAccount *account)
{
	char *sig_arg;
	char *sig;
	char *url;
	SoupMessage *msg;

	/* create soup session, if we haven't got one yet */
	if (account->priv->soup_session == NULL) {
		account->priv->soup_session =
			soup_session_async_new_with_options (SOUP_SESSION_ADD_FEATURE_BY_TYPE,
                                                             SOUP_TYPE_GNOME_FEATURES_2_26,
                                                             NULL);
	}

	/* request a token */
	sig_arg = g_strdup_printf ("api_key%smethodauth.getToken%s", LASTFM_API_KEY, LASTFM_API_SECRET);
	sig = mkmd5 (sig_arg);
	url = g_strdup_printf ("%s?method=auth.getToken&api_key=%s&api_sig=%s",
	                       LASTFM_API_URL, LASTFM_API_KEY, sig);

	msg = soup_message_new ("GET", url);

	rb_debug ("requesting authorisation token");
	soup_session_queue_message (account->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_account_got_token_cb,
	                            account);

	g_free (sig_arg);
	g_free (sig);
	g_free (url);
}

/* Request callbacks */
static void
rb_audioscrobbler_account_got_token_cb (SoupSession *session,
                                        SoupMessage *msg,
                                        gpointer user_data)
{
	RBAudioscrobblerAccount *account = RB_AUDIOSCROBBLER_ACCOUNT (user_data);

	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code) && msg->response_body->length != 0) {
		char **pre_split;
		char **post_split;
		char *url;

		/* parse the response */
		pre_split = g_strsplit (msg->response_body->data, "<token>", -1);
		post_split = g_strsplit (pre_split[1], "</token>", -1);
		account->priv->auth_token = g_strdup (post_split[0]);
		rb_debug ("granted auth token \"%s\"", account->priv->auth_token);

		/* send the user to the web page using the token */
		url = g_strdup_printf ("%s?api_key=%s&token=%s",
		                       LASTFM_AUTH_URL,
		                       LASTFM_API_KEY,
		                       account->priv->auth_token);
		rb_debug ("sending user to %s", url);
		gtk_show_uri (NULL, url, GDK_CURRENT_TIME, NULL);

		/* add timeout which will ask for session key */
		account->priv->session_key_timeout_id =
			g_timeout_add_seconds (SESSION_KEY_REQUEST_TIMEOUT,
			                       rb_audioscrobbler_account_request_session_key_timeout_cb,
			                       account);

		g_strfreev (pre_split);
		g_strfreev (post_split);
		g_free (url);
	}
}

static void
rb_audioscrobbler_account_got_session_key_cb (SoupSession *session,
                                              SoupMessage *msg,
                                              gpointer user_data)
{
	RBAudioscrobblerAccount *account;

	account = RB_AUDIOSCROBBLER_ACCOUNT (user_data);

	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code) && msg->response_body->length != 0) {
		char **pre_split;
		char **post_split;

		/* parse the response */
		pre_split = g_strsplit (msg->response_body->data, "<key>", -1);
		post_split = g_strsplit (pre_split[1], "</key>", -1);
		account->priv->session_key = g_strdup (post_split[0]);
		rb_debug ("granted session key \"%s\"", account->priv->session_key);
		rb_audioscrobbler_account_save_session_key (account);

		/* remove timeout callback */
		g_source_remove (account->priv->session_key_timeout_id);
		account->priv->session_key_timeout_id = 0;

		/* delete authorisation token */
		g_free (account->priv->auth_token);
		account->priv->auth_token = NULL;

		g_strfreev (pre_split);
		g_strfreev (post_split);
	} else {
		rb_debug ("error retrieving session key");
	}
}

/* Periodically sends a request for the session key */
static gboolean
rb_audioscrobbler_account_request_session_key_timeout_cb (gpointer user_data)
{
	RBAudioscrobblerAccount *account;
	char *sig_arg;
	char *sig;
	char *url;
	SoupMessage *msg;

	g_assert (RB_IS_AUDIOSCROBBLER_ACCOUNT (user_data));
	account = RB_AUDIOSCROBBLER_ACCOUNT (user_data);

	g_assert (account->priv->auth_token != NULL);

	sig_arg = g_strdup_printf ("api_key%smethodauth.getSessiontoken%s%s",
	                           LASTFM_API_KEY,
	                           account->priv->auth_token,
	                           LASTFM_API_SECRET);
	sig = mkmd5 (sig_arg);
	url = g_strdup_printf ("%s?method=auth.getSession&api_key=%s&token=%s&api_sig=%s",
	                       LASTFM_API_URL,
	                       LASTFM_API_KEY,
	                       account->priv->auth_token,
	                       sig);

	msg = soup_message_new ("GET", url);

	rb_debug ("requesting session key");
	soup_session_queue_message (account->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_account_got_session_key_cb,
	                            account);

	g_free (sig_arg);
	g_free (sig);
	g_free (url);

	return TRUE;
}