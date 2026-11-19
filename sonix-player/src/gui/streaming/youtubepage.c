#include "youtubepage.h"

#include <dirent.h>
#include <errno.h>
#include <utime.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdatomic.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/gui/library/browser.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/keyboard.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/toast.h"
#include "src/gui/nowplaying/player.h"
#include "src/system/device/system.h"
#include "src/system/net/http.h"
#include "src/system/net/wifi.h"
#include "src/system/playback/device_state.h"
#include "src/system/playback/playlist.h"

lv_obj_t *youtube_screen;
static lv_obj_t *search_screen, *field;
static keyboard_t *keyboard;

typedef struct {
	lv_obj_t *screen, *list;
	unsigned generation;
	char kind[16], query[301];
	int offset;
	char ids[40][12];
	char title[180];
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

static bool config(char *base, size_t size, char *header, size_t header_size) {
	char file[512], token[100];
	const char *root = playlist_card_root();
	if (!root || !*root) return false;
	snprintf(file, sizeof(file), "%s/youtube-music.conf", root);
	FILE *f = fopen(file, "r");
	if (!f) return false;
	bool ok = fgets(base, size, f) && fgets(token, sizeof(token), f);
	fclose(f);
	if (!ok) return false;
	base[strcspn(base, "\r\n")] = 0;
	token[strcspn(token, "\r\n")] = 0;
	if (strncmp(base, "https://", 8) || strlen(token) != 64 || strspn(token, "0123456789abcdef") != 64)
		return false;
	snprintf(header, header_size, "Authorization: Bearer %s\r\n", token);
	return true;
}

static bool valid_id(const char *id) {
	return strlen(id) == 11 && strspn(id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == 11;
}

typedef struct {
	char path[512], id[12];
	bool ok;
} download_t;

static pthread_mutex_t download_lock = PTHREAD_MUTEX_INITIALIZER;

static bool selected(const char *path) {
	char current[512];
	return playlist_current_path(current, sizeof(current)) && !strcmp(current, path);
}

// Only evict complete files from the private music cache. The worker holds
// download_lock, so a partially received file never enters this accounting.
static bool cache_room(const char *path, long incoming) {
	const char *slash = strrchr(path, '/');
	char dir[512];
	if (!slash || (size_t)(slash-path) >= sizeof(dir)) return false;
	snprintf(dir, sizeof(dir), "%.*s", (int)(slash-path), path);
	for (;;) {
		DIR *d = opendir(dir);
		if (!d) return false;
		long long used = 0;
		time_t oldest = 0;
		char victim[512] = "";
		struct dirent *entry;
		while ((entry = readdir(d))) {
			const char *name = entry->d_name;
			if (strlen(name) != 16 || strcmp(name+11, ".opus")) continue;
			char file[512];
			if (snprintf(file, sizeof(file), "%s/%s", dir, name) >= (int)sizeof(file)) continue;
			struct stat st;
			if (stat(file, &st) || !S_ISREG(st.st_mode)) continue;
			used += st.st_size;
			if (!victim[0] || st.st_mtime < oldest) {
				oldest = st.st_mtime;
				strcpy(victim, file);
			}
		}
		closedir(d);
		if (used + incoming <= 512LL * 1024 * 1024) return true;
		if (!victim[0] || unlink(victim)) return false;
	}
}

static bool fetch_opus(download_t *job, bool offline) {
	char base[240], header[128], url[512], part[530];
	if (!config(base, sizeof(base), header, sizeof(header))) return false;
	snprintf(url, sizeof(url), "%s/track-ticket/%s", base, job->id);
	char *ticket = NULL;
	size_t bytes = 0;
	int status = 0;
	bool ok = http_get_ex(url, header, false, &ticket, &bytes, 200, 20, &status);
	if (!ok || status != 200 || !ticket || strncmp(ticket, "track/", 6)) {
		free(ticket);
		return false;
	}
	snprintf(url, sizeof(url), "%s/%s", base, ticket);
	free(ticket);
	http_stream_t stream;
	if (!http_stream_open(&stream, url, 110)) return false;
	snprintf(part, sizeof(part), "%s.part", job->path);
	FILE *out = fopen(part, "wb");
	long received = 0;
	const long max_size = 256L * 1024 * 1024;
	struct statvfs space;
	bool valid_length = stream.content_length > 0 && stream.content_length <= max_size;
	bool room = valid_length && (offline ? out && !statvfs(part, &space) &&
		(unsigned long long)space.f_bavail * space.f_frsize >= (unsigned long long)stream.content_length :
		cache_room(job->path, stream.content_length));
	ok = out && room;
	while (ok && received < stream.content_length) {
		char buffer[16384];
		int n = http_stream_read(&stream, buffer, sizeof(buffer));
		ok = n > 0 && (received || (n >= 4 && !memcmp(buffer, "OggS", 4))) &&
			 fwrite(buffer, 1, n, out) == (size_t)n;
		if (ok) received += n;
	}
	if (out && fclose(out)) ok = false;
	ok = ok && received == stream.content_length;
	http_stream_close(&stream);
	if (ok && rename(part, job->path) == 0) return true;
	unlink(part);
	return false;
}

static void downloaded(void *arg) {
	download_t *job = arg;
	if (selected(job->path)) {
		if (job->ok) {
			device_state_play_queue_index(playlist_current_index());
			player_refresh_now_playing();
		} else gui_notify_popup("Cannot load YouTube Music track");
	}
	free(job);
}

static void *download_worker(void *arg) {
	download_t *job = arg;
	pthread_mutex_lock(&download_lock);
	job->ok = !access(job->path, R_OK) || fetch_opus(job, false);
	pthread_mutex_unlock(&download_lock);
	if (!gui_post(downloaded, job)) free(job);
	return NULL;
}

static bool prepare_opus(const char *path) {
	const char *root = playlist_card_root();
	if (!root || !*root) return true;
	char prefix[512];
	snprintf(prefix, sizeof(prefix), "%s/.local/youtube/", root);
	if (strncmp(path, prefix, strlen(prefix))) return true;
	if (!access(path, R_OK)) {
		utime(path, NULL);
		return true;
	}
	const char *name = path + strlen(prefix);
	if (strlen(name) != 16 || strcmp(name + 11, ".opus")) return false;
	download_t *job = calloc(1, sizeof(*job));
	if (!job) return false;
	memcpy(job->id, name, 11);
	snprintf(job->path, sizeof(job->path), "%s", path);
	device_state_stop();
	pthread_t worker;
	if (valid_id(job->id) && !pthread_create(&worker, NULL, download_worker, job))
		pthread_detach(worker);
	else free(job);
	return false;
}

// Some touch releases arrive as clicks at the edge of a scrolling list.
static lv_point_t press_start;
static bool dragged;
static void track_touch(lv_event_t *e) {
	lv_indev_t *in = lv_indev_active();
	if (!in || lv_indev_get_type(in) != LV_INDEV_TYPE_POINTER) return;
	lv_point_t point;
	lv_indev_get_point(in, &point);
	if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
		press_start = point;
		dragged = lv_indev_get_scroll_obj(in) != NULL;
	} else if (abs(point.y - press_start.y) > 10 || abs(point.x - press_start.x) > 10 ||
			   lv_indev_get_scroll_obj(in)) dragged = true;
}

static bool drag_click(void) {
	lv_indev_t *in = lv_indev_active();
	return in && lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER && dragged;
}

static void guard_row(lv_obj_t *row) {
	lv_obj_add_event_cb(row, track_touch, LV_EVENT_PRESSED, NULL);
	lv_obj_add_event_cb(row, track_touch, LV_EVENT_PRESSING, NULL);
	lv_obj_add_event_cb(row, track_touch, LV_EVENT_RELEASED, NULL);
}

static void play_cb(lv_event_t *e) {
	if (drag_click()) return;
	results_t *view = lv_screen_active() == tracks.screen ? &tracks : &catalog;
	int index = (int)(intptr_t)lv_event_get_user_data(e);
	const char *root = playlist_card_root();
	if (!root || !*root || index < 0 || index >= view->count) return;
	char dir[512], paths[40][512];
	const char *queue[40];
	snprintf(dir, sizeof(dir), "%s/.local/youtube", root);
	if (mkdir(dir, 0755) && errno != EEXIST) {
		gui_notify_popup("Cannot create YouTube Music cache");
		return;
	}
	for (int i = 0; i < view->count; i++) {
		snprintf(paths[i], sizeof(paths[i]), "%s/.local/youtube/%s.opus", root, view->ids[i]);
		queue[i] = paths[i];
	}
	device_state_play_list(queue, view->count, index);
	player_refresh_now_playing();
	player_sheet_open(true);
}

static void playlist_cb(lv_event_t *e) {
	if (drag_click()) return;
	const char *id = lv_event_get_user_data(e);
	lv_obj_t *row = lv_event_get_current_target(e);
	snprintf(tracks.title, sizeof(tracks.title), "%s", lv_label_get_text(settingsrow_name_label(row)));
	request_page(&tracks, "playlist", id, 0);
}

static void free_id(lv_event_t *e) {
	free(lv_event_get_user_data(e));
}

static void page_cb(lv_event_t *e) {
	if (drag_click()) return;
	results_t *view = lv_screen_active() == tracks.screen ? &tracks : &catalog;
	int offset = view->offset + (int)(intptr_t)lv_event_get_user_data(e);
	request_page(view, view->kind, view->query, offset);
}

static void list_message(lv_obj_t *list, const char *message) {
	lv_obj_t *label = lv_label_create(list);
	lv_obj_add_style(label, &theme_style_text, 0);
	lv_label_set_text(label, message);
}

static bool catalog_wifi_ready(void) {
#ifdef HOST_BUILD
	return true; // The desktop simulator uses the host network.
#else
	return wifi_interface_address(NULL, 0);
#endif
}

// Waking the display starts association asynchronously. DHCP and the resolver
// may become ready at different times, so retry transport failures only.
static const char *load_catalog(const char *url, const char *header, char **body) {
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		if (catalog_wifi_ready()) {
			size_t length = 0;
			int status = 0;
			if (http_get_ex(url, header, false, body, &length, 65536, 35, &status) && status == 200)
				return NULL;
			free(*body);
			*body = NULL;
			if (status == 409) return "Link Google on Rabisu to open your library";
			if (status == 401) return "Check the device key on the SD card";
			if (status > 0) return "YouTube Music unavailable";
			sleep(1);
		} else {
			if (!wifi_get_enabled()) return "Turn on Wi-Fi to open YouTube Music";
			usleep(250000);
		}
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while (now.tv_sec - start.tv_sec < 30);
	return catalog_wifi_ready() ? "Cannot reach YouTube Music" : "Wi-Fi did not connect";
}

typedef struct {
	char kind[16], query[301], folder[320], message[128];
} offline_job_t;

static atomic_bool offline_busy, offline_cancel;

bool youtube_offline_downloading(void) { return atomic_load(&offline_busy); }

static void safe_name(char *out, size_t size, const char *text) {
	size_t n = 0;
	while (*text && n + 1 < size) {
		unsigned char c = *text;
		size_t bytes = c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
		if (n + bytes >= size || strlen(text) < bytes) break;
		if (bytes == 1 && (c < 32 || strchr("/\\:*?\"<>|", c))) out[n++] = '_';
		else { memcpy(out + n, text, bytes); n += bytes; }
		text += bytes;
	}
	while (n && (out[n-1] == '.' || out[n-1] == ' ')) n--;
	if (!n) out[n++] = '_';
	out[n] = 0;
}

static bool offline_root(char *dir, size_t size) {
	const char *root = playlist_card_root();
	return root && *root && snprintf(dir, size, "%s/YouTube Music", root) < (int)size &&
		(mkdir(dir, 0755) == 0 || errno == EEXIST);
}

static void offline_open(lv_event_t *e) {
	(void)e;
	char dir[320];
	if (!offline_root(dir, sizeof(dir))) { gui_notify_popup("Check the SD card"); return; }
	browser_open_dir_public(dir);
	switch_screen(browser_screen);
}

static void offline_done(void *arg) {
	offline_job_t *job = arg;
	atomic_store(&offline_busy, false);
	gui_notify_popup(job->message);
	free(job);
}

static void *offline_worker(void *arg) {
	offline_job_t *job = arg;
	char base[240], header[128], query[910], url[1450];
	int saved = 0;
	const char *error = NULL;
	bool more = true;
	if (!config(base, sizeof(base), header, sizeof(header))) error = "Check youtube-music.conf";
	http_url_encode(job->query, query, sizeof(query));
	for (int offset = 0; more && !error && !atomic_load(&offline_cancel); offset += 40) {
		snprintf(url, sizeof(url), "%s/catalog/%s?q=%s&offset=%d", base, job->kind, query, offset);
		char *body = NULL;
		error = load_catalog(url, header, &body);
		more = false;
		char *next = NULL;
		for (char *line = body ? strtok_r(body, "\n", &next) : NULL;
			 line && !error && !atomic_load(&offline_cancel); line = strtok_r(NULL, "\n", &next)) {
			char *fields = NULL;
			char *kind = strtok_r(line, "\t", &fields);
			char *id = strtok_r(NULL, "\t", &fields);
			char *title = strtok_r(NULL, "\t", &fields);
			if (kind && !strcmp(kind, "more")) { more = true; continue; }
			if (!kind || strcmp(kind, "song") || !id || !valid_id(id) || !title) continue;
			char name[145];
			safe_name(name, sizeof(name), title);
			download_t track = {0};
			strcpy(track.id, id);
			snprintf(track.path, sizeof(track.path), "%s/%s [%s].opus", job->folder, name, id);
			if (access(track.path, R_OK) && !fetch_opus(&track, true)) error = "Check Wi-Fi and free space";
			else saved++;
		}
		free(body);
	}
	snprintf(job->message, sizeof(job->message), "%d tracks available offline. %s", saved,
			 error ? error : atomic_load(&offline_cancel) ? "Download stopped" : "Download complete");
	if (!gui_post(offline_done, job)) { atomic_store(&offline_busy, false); free(job); }
	return NULL;
}

static void save_offline(lv_event_t *e) {
	if (drag_click()) return;
	if (youtube_offline_downloading()) { gui_notify_popup("An offline download is in progress"); return; }
	results_t *v = lv_event_get_user_data(e);
	offline_job_t *job = calloc(1, sizeof(*job));
	if (!job) return;
	char dir[160], name[95];
	safe_name(name, sizeof(name), v == &tracks ? v->title :
			  !strcmp(v->kind, "likes") ? "Liked songs" : "Library songs");
	if (!offline_root(dir, sizeof(dir))) goto fail;
	int len = v == &tracks ? snprintf(job->folder, sizeof(job->folder), "%s/%s [%s]", dir, name, v->query) :
		snprintf(job->folder, sizeof(job->folder), "%s/%s", dir, name);
	if (len >= (int)sizeof(job->folder) || (mkdir(job->folder, 0755) && errno != EEXIST)) goto fail;
	strcpy(job->kind, v->kind);
	strcpy(job->query, v->query);
	atomic_store(&offline_cancel, false);
	atomic_store(&offline_busy, true);
	pthread_t worker;
	if (!pthread_create(&worker, NULL, offline_worker, job)) {
		pthread_detach(worker);
		gui_notify_popup("Saving tracks to Offline music");
		return;
	}
	atomic_store(&offline_busy, false);
fail:
	free(job);
	gui_notify_popup("Cannot start offline download");
}

static void cancel_offline(lv_event_t *e) {
	(void)e;
	bool busy = youtube_offline_downloading();
	if (busy) atomic_store(&offline_cancel, true);
	gui_notify_popup(busy ? "Stopping after the current track" : "No download in progress");
}

static void show_response(void *arg) {
	request_t *r = arg;
	results_t *v = r->view;
	if (v->generation != r->generation) goto done;
	lv_obj_clean(v->list);
	v->count = 0;
	if (r->error) {
		list_message(v->list, r->error);
		goto done;
	}
	if (r->body && *r->body && (!strcmp(r->kind, "likes") || !strcmp(r->kind, "songs") || !strcmp(r->kind, "playlist")))
		guard_row(settingsrow_action(v->list, "Save list offline", save_offline, v));
	if (r->offset) guard_row(settingsrow_add(v->list, "Previous page", NULL, page_cb, (void *)(intptr_t)-40));
	char *next = NULL;
	for (char *line = strtok_r(r->body, "\n", &next); line; line = strtok_r(NULL, "\n", &next)) {
		char *fields = NULL;
		char *kind = strtok_r(line, "\t", &fields);
		char *id = strtok_r(NULL, "\t", &fields);
		char *title = strtok_r(NULL, "\t", &fields);
		char *path = strtok_r(NULL, "\t", &fields);
		if (!kind || !id || !title || !path) continue;
		if (!strcmp(kind, "more")) {
			guard_row(settingsrow_add(v->list, "Next page", NULL, page_cb, (void *)(intptr_t)40));
		} else if (!strcmp(kind, "playlist")) {
			char *copy = strdup(id);
			if (!copy) continue;
			lv_obj_t *row = settingsrow_add(v->list, title, NULL, playlist_cb, copy);
			guard_row(row);
			lv_obj_add_event_cb(row, free_id, LV_EVENT_DELETE, copy);
		} else if (!strcmp(kind, "song") && !strncmp(path, "audio/", 6) && v->count < 40) {
			if (!valid_id(id)) continue;
			int index = v->count++;
			strcpy(v->ids[index], id);
			lv_obj_t *row = settingsrow_add(v->list, title, NULL, play_cb, (void *)(intptr_t)index);
			guard_row(row);
			settingsrow_name_lines(row, 2);
		}
	}
	if (!lv_obj_get_child_count(v->list))
		list_message(v->list, "No results");
done:
	free(r->body);
	free(r);
}

static void *fetch_page(void *arg) {
	request_t *r = arg;
	char header[128], query[910], url[1450];
	if (!config(r->base, sizeof(r->base), header, sizeof(header))) {
		r->error = "Copy youtube-music.conf to the SD card";
	} else {
		http_url_encode(r->query, query, sizeof(query));
		snprintf(url, sizeof(url), "%s/catalog/%s?q=%s&offset=%d", r->base, r->kind, query, r->offset);
		r->error = load_catalog(url, header, &r->body);
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
	list_message(v->list, catalog_wifi_ready() ? "Loading…" : "Waiting for Wi-Fi…");
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
	device_state_add_prepare_cb(prepare_opus);
	youtube_screen = lv_obj_create(NULL);
	search_screen = lv_obj_create(NULL);
	catalog.screen = lv_obj_create(NULL);
	tracks.screen = lv_obj_create(NULL);
	lv_obj_t *home = settingsrow_page(youtube_screen, cfg, "YouTube Music");
	settingsrow_add(home, "Search", NULL, switch_screen_cb, search_screen);
	settingsrow_add(home, "Library songs", NULL, browse_cb, "songs");
	settingsrow_add(home, "Liked songs", NULL, browse_cb, "likes");
	settingsrow_add(home, "My playlists", NULL, browse_cb, "playlists");
	settingsrow_add(home, "Offline music", NULL, offline_open, NULL);
	settingsrow_action(home, "Stop offline download", cancel_offline, NULL);
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
