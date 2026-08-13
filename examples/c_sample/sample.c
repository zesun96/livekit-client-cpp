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

static void on_reconnecting(void* user_data, lk_room_t* room) {
	(void)user_data;
	(void)room;
	puts("Reconnecting event received");
}

static void on_reconnected(void* user_data, lk_room_t* room) {
	(void)user_data;
	(void)room;
	puts("Reconnected event received");
}

static void on_disconnected(void* user_data, lk_room_t* room, lk_disconnect_reason_t reason) {
	(void)user_data;
	(void)room;
	printf("Disconnected event received, reason=%d\n", (int)reason);
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

static void on_track_subscription_failed(void* user_data, lk_room_t* room,
                                         const lk_track_publication_info_t* track,
                                         const lk_participant_info_t* participant,
                                         lk_subscription_error_t error) {
	(void)user_data;
	(void)room;
	printf("Track subscription failed for %s from %s: error=%d\n", track->sid,
	       participant->identity, (int)error);
}

static lk_rpc_handler_result_t on_echo_rpc(void* user_data, const lk_rpc_invocation_t* invocation) {
	lk_rpc_handler_result_t result = {0};
	(void)user_data;
	printf("RPC from %s: %s\n", invocation->caller_identity, invocation->payload);
	result.payload = invocation->payload;
	return result;
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
	callbacks.on_reconnecting = on_reconnecting;
	callbacks.on_reconnected = on_reconnected;
	callbacks.on_disconnected_with_reason = on_disconnected;
	callbacks.on_participant_connected = on_participant_connected;
	callbacks.on_track_published = on_track_published;
	callbacks.on_track_subscription_failed = on_track_subscription_failed;
	lk_room_set_callbacks(room, &callbacks);
	{
		const char* allowed_subscriber = getenv("LIVEKIT_ALLOWED_SUBSCRIBER");
		if (allowed_subscriber != NULL && allowed_subscriber[0] != '\0') {
			lk_participant_track_permission_t permission;
			lk_participant_track_permission_init(&permission);
			permission.participant_identity = allowed_subscriber;
			permission.allow_all = 1;
			if (lk_room_set_track_subscription_permissions(room, 0, &permission, 1) !=
			    LK_STATUS_OK) {
				fprintf(stderr, "Subscription permission failed: %s\n", lk_last_error());
			}
		}
	}
	if (lk_room_register_rpc_method(room, "c.echo", on_echo_rpc, NULL) != LK_STATUS_OK) {
		fprintf(stderr, "RPC registration failed: %s\n", lk_last_error());
	}

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

	if (argc >= 4) {
		lk_rpc_perform_options_t options;
		lk_rpc_result_t* result = NULL;
		lk_rpc_perform_options_init(&options);
		options.destination_identity = argv[3];
		options.method = "c.echo";
		options.payload = "hello from C";
		if (lk_room_perform_rpc(room, &options, &result) != LK_STATUS_OK) {
			fprintf(stderr, "RPC call failed: %s\n", lk_last_error());
		} else if (!lk_rpc_result_ok(result)) {
			fprintf(stderr, "RPC error: %u\n", lk_rpc_result_error_code(result));
		} else {
			const size_t required = lk_rpc_result_payload(result, NULL, 0);
			char* response = (char*)malloc(required);
			if (response != NULL) {
				lk_rpc_result_payload(result, response, required);
				printf("RPC response: %s\n", response);
				free(response);
			}
		}
		lk_rpc_result_destroy(result);
	}

	lk_room_disconnect(room);
	lk_room_destroy(room);
	lk_shutdown();
	return 0;
}
