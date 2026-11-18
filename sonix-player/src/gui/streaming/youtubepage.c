#include "youtubepage.h"

#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/gui/shell/icons.h"
#include "src/gui/shell/keyboard.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/gui/nowplaying/player.h"
#include "src/system/device/system.h"
#include "src/system/net/http.h"
#include "src/system/streaming/radio.h"

lv_obj_t *youtube_screen;
static lv_obj_t *search_screen, *field;
static keyboard_t *keyboard;

typedef struct {
	lv_obj_t *screen, *list;
	unsigned generation;
	char kind[16], query[301];
	int offset;
	radio_station_t stations[40];
	int count;
} results_t;
static results_t catalog, tracks;

typedef struct {
	results_t *view;
	unsigned generation;
	char kind[16], query[301], base[240];
	int offset;
	char *body;
	const char *error;
} request_t;

static void request_page(results_t *view, const char *kind, const char *query, int offset);

static void play_cb(lv_event_t *e) {
	radio_station_t *station = lv_event_get_user_data(e);
	results_t *view = lv_screen_active() == tracks.screen ? &tracks : &catalog;
	radio_set_list(view->stations, view->count);
	if (radio_play(station)) {
		player_refresh_now_playing();
		player_sheet_open(true);
	} else {
		gui_notify_popup("Cannot play this track");
	}
}

static void playlist_cb(lv_event_t *e) {
	const char *id = lv_event_get_user_data(e);
	request_page(&tracks, "playlist", id, 0);
}

static void free_id(lv_event_t *e) {
	free(lv_event_get_user_data(e));
}

static void page_cb(lv_event_t *e) {
	results_t *view = lv_screen_active() == tracks.screen ? &tracks : &catalog;
	int offset = view->offset + (int)(intptr_t)lv_event_get_user_data(e);
	request_page(view, view->kind, view->query, offset);
}

static void show_response(void *arg) {
	request_t *r = arg;
	results_t *v = r->view;
	if (v->generation != r->generation) goto done;
	lv_obj_clean(v->list);
	v->count = 0;
	if (r->error) {
		lv_label_set_text(lv_label_create(v->list), r->error);
		goto done;
	}
	if (r->offset) settingsrow_add(v->list, "Previous page", NULL, page_cb, (void *)(intptr_t)-40);
	char *next = NULL;
	for (char *line = strtok_r(r->body, "\n", &next); line; line = strtok_r(NULL, "\n", &next)) {
		char *fields = NULL;
		char *kind = strtok_r(line, "\t", &fields);
		char *id = strtok_r(NULL, "\t", &fields);
		char *title = strtok_r(NULL, "\t", &fields);
		char *path = strtok_r(NULL, "\t", &fields);
		if (!kind || !id || !title || !path) continue;
		if (!strcmp(kind, "more")) {
			settingsrow_add(v->list, "Next page", NULL, page_cb, (void *)(intptr_t)40);
		} else if (!strcmp(kind, "playlist")) {
			char *copy = strdup(id);
			if (!copy) continue;
			lv_obj_t *row = settingsrow_add(v->list, title, NULL, playlist_cb, copy);
			lv_obj_add_event_cb(row, free_id, LV_EVENT_DELETE, copy);
		} else if (!strcmp(kind, "song") && !strncmp(path, "audio/", 6) && v->count < 40) {
			radio_station_t *s = &v->stations[v->count++];
			memset(s, 0, sizeof(*s));
			snprintf(s->uuid, sizeof(s->uuid), "yt-%s", id);
			snprintf(s->name, sizeof(s->name), "%s", title);
			snprintf(s->url, sizeof(s->url), "%s/%s", r->base, path);
			snprintf(s->codec, sizeof(s->codec), "MP3");
			settingsrow_name_lines(settingsrow_add(v->list, title, NULL, play_cb, s), 2);
		}
	}
	if (!lv_obj_get_child_count(v->list))
		lv_label_set_text(lv_label_create(v->list), "No results");
done:
	free(r->body);
	free(r);
}

static void *fetch_page(void *arg) {
	request_t *r = arg;
	char file[512], token[100], header[128], query[910], url[1450];
	const char *root = storage_sd_root();
	snprintf(file, sizeof(file), "%s/youtube-music.conf", root ? root : "");
	FILE *f = root ? fopen(file, "r") : NULL;
	bool ok = f && fgets(r->base, sizeof(r->base), f) && fgets(token, sizeof(token), f);
	if (f) fclose(f);
	if (ok) {
		r->base[strcspn(r->base, "\r\n")] = 0;
		token[strcspn(token, "\r\n")] = 0;
		ok = !strncmp(r->base, "https://", 8) && strlen(token) == 64 &&
			 strspn(token, "0123456789abcdef") == 64;
	}
	if (!ok) {
		r->error = "Copy youtube-music.conf to the SD card";
	} else {
		http_url_encode(r->query, query, sizeof(query));
		snprintf(url, sizeof(url), "%s/catalog/%s?q=%s&offset=%d", r->base, r->kind, query, r->offset);
		snprintf(header, sizeof(header), "Authorization: Bearer %s\r\n", token);
		int status = 0;
		size_t length = 0;
		if (!http_get_ex(url, header, false, &r->body, &length, 65536, 35, &status) || status != 200)
			r->error = status == 409 ? "Link Google on Rabisu to open your library" :
					   status == 401 ? "Check the device key on the SD card" : "YouTube Music unavailable";
	}
	if (!gui_post(show_response, r)) {
		free(r->body);
		free(r);
	}
	return NULL;
}

static void request_page(results_t *v, const char *kind, const char *query, int offset) {
	request_t *r = calloc(1, sizeof(*r));
	if (!r) return;
	r->view = v;
	r->generation = ++v->generation;
	r->offset = v->offset = offset;
	snprintf(r->kind, sizeof(r->kind), "%s", kind);
	snprintf(r->query, sizeof(r->query), "%s", query);
	strcpy(v->kind, r->kind);
	strcpy(v->query, r->query);
	lv_obj_clean(v->list);
	lv_label_set_text(lv_label_create(v->list), "Loading…");
	if (lv_screen_active() != v->screen) switch_screen(v->screen);
	pthread_t worker;
	if (!pthread_create(&worker, NULL, fetch_page, r)) pthread_detach(worker);
	else {
		r->error = "Cannot start request";
		show_response(r);
	}
}

static void browse_cb(lv_event_t *e) {
	request_page(&catalog, lv_event_get_user_data(e), "", 0);
}

static void search_cb(lv_event_t *e) {
	(void)e;
	const char *query = lv_textarea_get_text(field);
	if (*query) request_page(&catalog, "search", query, 0);
}

void youtubepage_init(gui_config_t *cfg) {
	youtube_screen = lv_obj_create(NULL);
	search_screen = lv_obj_create(NULL);
	catalog.screen = lv_obj_create(NULL);
	tracks.screen = lv_obj_create(NULL);
	lv_obj_t *home = settingsrow_page(youtube_screen, cfg, "YouTube Music");
	settingsrow_add(home, "Search", NULL, switch_screen_cb, search_screen);
	settingsrow_add(home, "Library songs", NULL, browse_cb, "songs");
	settingsrow_add(home, "Liked songs", NULL, browse_cb, "likes");
	settingsrow_add(home, "My playlists", NULL, browse_cb, "playlists");
	catalog.list = settingsrow_page(catalog.screen, cfg, "YouTube Music");
	tracks.list = settingsrow_page(tracks.screen, cfg, "Playlist");
	lv_obj_add_style(search_screen, &theme_style_screen, 0);
	settingsrow_title(search_screen, cfg, "Search YouTube Music");
	field = lv_textarea_create(search_screen);
	lv_textarea_set_one_line(field, true);
	lv_textarea_set_max_length(field, 90);
	lv_obj_set_size(field, cfg->screen_width - 2 * cfg->padding, 62);
	lv_obj_align(field, LV_ALIGN_TOP_LEFT, cfg->padding, settingsrow_content_top(cfg));
	keyboard_style_caret(field);
	keyboard = keyboard_create(search_screen, cfg->screen_width, 316, field, &icon_search, NULL, search_cb, NULL);
	keyboard_set_visible(keyboard, true);
	switcher_attach_back_gesture(search_screen);
	switcher_attach_back_gesture(youtube_screen);
	switcher_attach_back_gesture(catalog.screen);
	switcher_attach_back_gesture(tracks.screen);
}
