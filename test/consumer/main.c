#include <livekit/capi/livekit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char* message) {
	fprintf(stderr, "%s: %s\n", message, lk_last_error());
	return 1;
}

static int verify_json_tracing(void) {
	static const char trace_path[] = "livekit-c-consumer-trace.json";
	remove(trace_path);
	lk_trace_options_t options;
	lk_trace_options_init(&options);
	if (options.struct_size != sizeof(options) || options.enabled != 0 ||
	    options.category_mask != LK_TRACE_CATEGORY_ALL) {
		fprintf(stderr, "Unexpected tracing option defaults\n");
		return 0;
	}
	options.enabled = 1;
	options.category_mask = LK_TRACE_CATEGORY_LIFECYCLE;
	if (lk_trace_set_options(&options) != LK_STATUS_OK ||
	    lk_trace_start_json_file(trace_path) != LK_STATUS_OK || lk_init() != LK_STATUS_OK ||
	    lk_shutdown() != LK_STATUS_OK || lk_trace_stop() != LK_STATUS_OK) {
		fprintf(stderr, "C trace JSON smoke failed: %s\n", lk_last_error());
		lk_trace_stop();
		remove(trace_path);
		return 0;
	}
	options.enabled = 0;
	lk_trace_set_options(&options);

	FILE* file = fopen(trace_path, "rb");
	if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
		if (file != NULL) {
			fclose(file);
		}
		remove(trace_path);
		fprintf(stderr, "C trace JSON file was not created\n");
		return 0;
	}
	const long size = ftell(file);
	if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		remove(trace_path);
		fprintf(stderr, "C trace JSON file could not be read\n");
		return 0;
	}
	char* contents = (char*)malloc((size_t)size + 1);
	if (contents == NULL || fread(contents, 1, (size_t)size, file) != (size_t)size) {
		free(contents);
		fclose(file);
		remove(trace_path);
		fprintf(stderr, "C trace JSON file could not be loaded\n");
		return 0;
	}
	contents[size] = '\0';
	fclose(file);
	const int valid = strstr(contents, "\"traceEvents\"") != NULL &&
	                  strstr(contents, "\"name\":\"runtime.init\"") != NULL &&
	                  strstr(contents, "\"ph\":\"B\"") != NULL &&
	                  strstr(contents, "\"ph\":\"E\"") != NULL &&
	                  strstr(contents, "\"displayTimeUnit\":\"ms\"") != NULL;
	free(contents);
	remove(trace_path);
	if (!valid) {
		fprintf(stderr, "C trace output was not Perfetto/Chrome Trace compatible\n");
	}
	return valid;
}

int main(void) {
	if (!verify_json_tracing()) {
		return 1;
	}
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
	lk_local_participant_snapshot_t* local_snapshot = NULL;
	if (lk_room_create_local_participant_snapshot(room, &local_snapshot) != LK_STATUS_OK ||
	    local_snapshot == NULL ||
	    lk_local_participant_snapshot_identity(local_snapshot, NULL, 0) != 1) {
		free(version);
		lk_local_participant_snapshot_destroy(local_snapshot);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "Disconnected local participant snapshot was unavailable\n");
		return 1;
	}
	lk_local_participant_snapshot_info_t local_snapshot_info;
	lk_local_participant_snapshot_info_init(&local_snapshot_info);
	if (lk_local_participant_snapshot_info(local_snapshot, &local_snapshot_info) != LK_STATUS_OK ||
	    local_snapshot_info.struct_size != sizeof(local_snapshot_info)) {
		free(version);
		lk_local_participant_snapshot_destroy(local_snapshot);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "Local participant snapshot info was unavailable\n");
		return 1;
	}
	lk_local_participant_snapshot_destroy(local_snapshot);
	lk_data_stream_writer_info_t writer_info;
	lk_data_stream_writer_info_init(&writer_info);
	if (writer_info.struct_size != sizeof(writer_info) ||
	    writer_info.kind != LK_DATA_STREAM_WRITER_KIND_TEXT) {
		free(version);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "DataStream writer info defaults were not initialized\n");
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
	lk_rtc_track_stats_t rtc_stats;
	lk_rtc_track_stats_init(&rtc_stats);
	if (rtc_stats.struct_size != sizeof(rtc_stats) ||
	    rtc_stats.direction != LK_RTC_STATS_DIRECTION_UNKNOWN) {
		free(version);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "RTC stats defaults were not initialized\n");
		return 1;
	}
	lk_room_connect_options_t connect_options;
	lk_room_connect_options_init(&connect_options);
	if (connect_options.struct_size != sizeof(connect_options) ||
	    connect_options.auto_subscribe != 1 || connect_options.join_retries != 3 ||
	    connect_options.reconnect_timeout_ms != 15000 ||
	    connect_options.continual_gathering_policy !=
	        LK_CONTINUAL_GATHERING_POLICY_GATHER_CONTINUALLY ||
	    connect_options.ice_transport_type != LK_ICE_TRANSPORT_TYPE_ALL) {
		free(version);
		lk_room_destroy(room);
		lk_shutdown();
		fprintf(stderr, "Room connect options did not expose the expected defaults\n");
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
