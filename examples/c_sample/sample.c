#include "livekit/capi/livekit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void on_connected(void* user_data, lk_room_t* room) {
	(void)user_data;
	(void)room;
	puts("Connected event received");
}

static void on_participant_connected(void* user_data, lk_room_t* room,
                                     const lk_participant_info_t* participant) {
	(void)user_data;
	(void)room;
	printf("Participant connected: %s (%s)\n", participant->identity, participant->sid);
}

static void on_track_published(void* user_data, lk_room_t* room,
                               const lk_track_publication_info_t* track,
                               const lk_participant_info_t* participant) {
	(void)user_data;
	(void)room;
	printf("Track published by %s: %s (%s)\n", participant->identity, track->name, track->sid);
}

static int read_string(size_t (*getter)(const lk_room_t*, char*, size_t), const lk_room_t* room,
                       char** output) {
	const size_t required = getter(room, NULL, 0);
	if (required == 0) {
		return 0;
	}
	*output = (char*)malloc(required);
	if (*output == NULL) {
		return 0;
	}
	getter(room, *output, required);
	return 1;
}

int main(int argc, char** argv) {
	const char* url = argc > 1 ? argv[1] : getenv("LIVEKIT_URL");
	const char* token = argc > 2 ? argv[2] : getenv("LIVEKIT_TOKEN");
	if (url == NULL || token == NULL || url[0] == '\0' || token[0] == '\0') {
		fprintf(stderr, "Usage: %s <url> <token>\n", argv[0]);
		return 2;
	}

	if (lk_init() != LK_STATUS_OK) {
		fprintf(stderr, "LiveKit initialization failed: %s\n", lk_last_error());
		return 1;
	}

	lk_room_t* room = NULL;
	if (lk_room_create(&room) != LK_STATUS_OK) {
		fprintf(stderr, "Room creation failed: %s\n", lk_last_error());
		lk_shutdown();
		return 1;
	}

	lk_room_callbacks_t callbacks;
	lk_room_callbacks_init(&callbacks);
	callbacks.on_connected = on_connected;
	callbacks.on_participant_connected = on_participant_connected;
	callbacks.on_track_published = on_track_published;
	lk_room_set_callbacks(room, &callbacks);

	if (lk_room_connect(room, url, token) != LK_STATUS_OK) {
		fprintf(stderr, "Connection failed: %s\n", lk_last_error());
		lk_room_destroy(room);
		lk_shutdown();
		return 1;
	}

	for (unsigned attempt = 0; attempt < 100 && !lk_room_is_connected(room); ++attempt) {
		sleep_ms(100);
	}
	if (!lk_room_is_connected(room)) {
		fprintf(stderr, "Timed out waiting for the RTC connection\n");
		lk_room_destroy(room);
		lk_shutdown();
		return 1;
	}

	char* identity = NULL;
	if (read_string(lk_local_participant_identity, room, &identity)) {
		printf("Connected as %s\n", identity);
		free(identity);
	}

	lk_room_disconnect(room);
	lk_room_destroy(room);
	lk_shutdown();
	return 0;
}
