#include <livekit/capi/livekit.h>

#include <stdio.h>
#include <stdlib.h>

static int fail(const char* message) {
	fprintf(stderr, "%s: %s\n", message, lk_last_error());
	return 1;
}

int main(void) {
	const size_t version_size = lk_version(NULL, 0);
	if (version_size <= 1) {
		return fail("LiveKit SDK returned an empty version");
	}
	char* version = (char*)malloc(version_size);
	if (version == NULL) {
		fprintf(stderr, "Failed to allocate the version buffer\n");
		return 1;
	}
	if (lk_version(version, version_size) != version_size) {
		free(version);
		return fail("Failed to read the LiveKit SDK version");
	}

	if (lk_room_create(NULL) != LK_STATUS_INVALID_ARGUMENT) {
		free(version);
		fprintf(stderr, "Invalid room output was accepted\n");
		return 1;
	}
	lk_error_info_t error;
	lk_error_info_init(&error);
	if (lk_last_error_info(&error) != LK_STATUS_OK || error.domain != LK_ERROR_DOMAIN_STATUS ||
	    error.code != LK_STATUS_INVALID_ARGUMENT || error.message_size <= 1) {
		free(version);
		fprintf(stderr, "Structured C API error reporting is unavailable\n");
		return 1;
	}

	if (lk_init() != LK_STATUS_OK) {
		free(version);
		return fail("Failed to initialize LiveKit");
	}
	lk_room_t* room = NULL;
	if (lk_room_create(&room) != LK_STATUS_OK) {
		fprintf(stderr, "Failed to create a room: %s\n", lk_last_error());
		free(version);
		lk_shutdown();
		return 1;
	}
	if (lk_room_state(room) != LK_ROOM_STATE_DISCONNECTED || lk_room_sid(room, NULL, 0) != 1) {
		free(version);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "New room did not expose the expected disconnected state\n");
		return 1;
	}
	if (lk_local_participant_identity(room, NULL, 0) != 1) {
		free(version);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "Disconnected room did not expose an empty participant identity\n");
		return 1;
	}
	lk_media_stream_options_t media_stream_options;
	lk_media_stream_options_init(&media_stream_options);
	if (media_stream_options.struct_size != sizeof(media_stream_options) ||
	    media_stream_options.capacity != 16) {
		free(version);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "Media stream options did not expose the expected defaults\n");
		return 1;
	}
	lk_audio_stream_t* audio_stream = NULL;
	if (lk_room_create_audio_stream(room, "missing-participant", "TR_missing",
	                                &media_stream_options,
	                                &audio_stream) != LK_STATUS_OPERATION_FAILED ||
	    audio_stream != NULL) {
		free(version);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "Missing remote audio track unexpectedly created a stream\n");
		return 1;
	}
	if (lk_local_participant_identity(NULL, NULL, 0) != 0) {
		free(version);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "Null room unexpectedly exposed a local participant identity\n");
		return 1;
	}
	lk_error_info_init(&error);
	if (lk_last_error_info(&error) != LK_STATUS_OK || error.domain != LK_ERROR_DOMAIN_STATUS ||
	    error.code != LK_STATUS_INVALID_ARGUMENT) {
		free(version);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "Two-phase string failure did not expose a structured error\n");
		return 1;
	}

	lk_room_destroy(room);
	if (lk_shutdown() != LK_STATUS_OK) {
		free(version);
		return fail("Failed to shut down LiveKit");
	}
	printf("LiveKit C API %s\n", version);
	free(version);
	return 0;
}
