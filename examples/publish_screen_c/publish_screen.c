#include "livekit/capi/livekit.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(unsigned milliseconds) { Sleep(milliseconds); }
#else
#include <time.h>
static void sleep_ms(unsigned milliseconds) {
	struct timespec duration;
	duration.tv_sec = milliseconds / 1000;
	duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	nanosleep(&duration, NULL);
}
#endif

static char* copy_source_id(const lk_screen_source_list_t* sources, size_t index) {
	const size_t size = lk_screen_source_list_id(sources, index, NULL, 0);
	char* value = size > 0 ? (char*)malloc(size) : NULL;
	if (value != NULL) {
		lk_screen_source_list_id(sources, index, value, size);
	}
	return value;
}

static char* first_monitor_id(void) {
	lk_screen_source_list_t* sources = NULL;
	char* selected = NULL;
	if (lk_screen_source_list_create(&sources) != LK_STATUS_OK) {
		return NULL;
	}
	for (size_t index = 0; index < lk_screen_source_list_count(sources); ++index) {
		lk_screen_source_info_t info = {0};
		const size_t label_size = lk_screen_source_list_label(sources, index, NULL, 0);
		char* label = label_size > 0 ? (char*)malloc(label_size) : NULL;
		info.struct_size = sizeof(info);
		if (label != NULL) {
			lk_screen_source_list_label(sources, index, label, label_size);
		}
		if (lk_screen_source_list_info(sources, index, &info) == LK_STATUS_OK) {
			char* id = copy_source_id(sources, index);
			printf("%s %s %s (%ux%u)\n",
			       info.kind == LK_SCREEN_SOURCE_KIND_MONITOR ? "monitor" : "window",
			       id != NULL ? id : "", label != NULL ? label : "", info.width, info.height);
			if (selected == NULL && info.kind == LK_SCREEN_SOURCE_KIND_MONITOR) {
				selected = id;
				id = NULL;
			}
			free(id);
		}
		free(label);
	}
	lk_screen_source_list_destroy(sources);
	return selected;
}

int main(int argc, char** argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <ws-url> <token> [monitor-or-window-id]\n", argv[0]);
		return 2;
	}
	if (lk_init() != LK_STATUS_OK) {
		fprintf(stderr, "LiveKit initialization failed: %s\n", lk_last_error());
		return 1;
	}

	char* default_source_id = argc > 3 ? NULL : first_monitor_id();
	const char* source_id = argc > 3 ? argv[3] : default_source_id;
	lk_screen_capture_options_t options;
	lk_screen_capture_options_init(&options);
	options.source_id = source_id;
	lk_video_source_t* source = NULL;
	if (source_id == NULL || lk_video_source_create_screen(&options, &source) != LK_STATUS_OK) {
		fprintf(stderr, "Screen capture failed: %s\n", lk_last_error());
		free(default_source_id);
		lk_shutdown();
		return 1;
	}
	free(default_source_id);

	lk_room_t* room = NULL;
	lk_local_track_t* track = NULL;
	int result = 1;
	if (lk_room_create(&room) != LK_STATUS_OK ||
	    lk_room_connect(room, argv[1], argv[2]) != LK_STATUS_OK) {
		fprintf(stderr, "Connection failed: %s\n", lk_last_error());
		goto cleanup;
	}
	for (unsigned attempt = 0; attempt < 100 && !lk_room_is_connected(room); ++attempt) {
		sleep_ms(100);
	}
	if (!lk_room_is_connected(room)) {
		fprintf(stderr, "Timed out waiting for the RTC connection\n");
		goto cleanup;
	}
	if (lk_room_create_video_track(room, "screen", source, &track) != LK_STATUS_OK ||
	    lk_local_track_publish_screen_share_video(room, track, NULL) != LK_STATUS_OK) {
		fprintf(stderr, "Screen publish failed: %s\n", lk_last_error());
		goto cleanup;
	}
	puts("Publishing screen for 10 seconds");
	sleep_ms(10000);
	result = 0;

cleanup:
	if (track != NULL) {
		lk_local_track_unpublish(track, 1);
		lk_local_track_destroy(track);
	}
	if (room != NULL) {
		lk_room_disconnect(room);
		lk_room_destroy(room);
	}
	lk_video_source_screen_stop(source);
	lk_video_source_destroy(source);
	lk_shutdown();
	return result;
}
