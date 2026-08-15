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

static void on_connection_state(void* user_data, lk_room_t* room, lk_room_state_t state) {
	(void)user_data;
	(void)room;
	printf("Connection state changed: %d\n", (int)state);
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

static void on_participant_permissions(void* user_data, lk_room_t* room,
                                       const lk_participant_permissions_t* previous_permissions,
                                       const lk_participant_permissions_t* permissions,
                                       const lk_participant_info_t* participant) {
	(void)user_data;
	(void)room;
	printf("Participant permissions changed: %s, publish=%d->%d, subscribe=%d->%d\n",
	       participant->identity, previous_permissions->can_publish, permissions->can_publish,
	       previous_permissions->can_subscribe, permissions->can_subscribe);
}

static void on_track_published(void* user_data, lk_room_t* room,
                               const lk_track_publication_info_t* track,
                               const lk_participant_info_t* participant) {
	(void)user_data;
	printf("Track published by %s: %s (%s)\n", participant->identity, track->name, track->sid);
	if (track->kind == LK_TRACK_KIND_VIDEO) {
		lk_remote_track_settings_t settings;
		lk_remote_track_settings_init(&settings);
		settings.has_video_quality = 1;
		settings.video_quality = LK_VIDEO_QUALITY_MEDIUM;
		settings.video_fps = 24;
		if (lk_room_update_remote_track_settings(room, participant->sid, track->sid, &settings) !=
		    LK_STATUS_OK) {
			fprintf(stderr, "Remote video settings failed: %s\n", lk_last_error());
		}
	}
}

static void on_local_track_subscribed(void* user_data, lk_room_t* room,
                                      const lk_track_publication_info_t* track,
                                      const lk_participant_info_t* participant) {
	(void)user_data;
	(void)room;
	printf("Local track has a subscriber: %s from %s\n", track->sid, participant->identity);
}

static void on_subscribed_quality_update(void* user_data, lk_room_t* room,
                                         const lk_track_publication_info_t* track,
                                         const lk_participant_info_t* participant,
                                         const lk_subscribed_quality_update_t* update) {
	(void)user_data;
	(void)room;
	printf("Subscribed quality update for %s from %s: codecs=%zu, legacy-qualities=%zu\n",
	       track->sid, participant->identity, update->codec_count, update->quality_count);
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

static void on_track_unsubscribed(void* user_data, lk_room_t* room,
                                  const lk_track_publication_info_t* track,
                                  const lk_participant_info_t* participant) {
	(void)user_data;
	(void)room;
	printf("Track unsubscribed: %s from %s\n", track->sid, participant->identity);
}

static void on_track_stream_state_changed(void* user_data, lk_room_t* room,
                                          const lk_track_publication_info_t* track,
                                          const lk_participant_info_t* participant,
                                          lk_track_stream_state_t state) {
	(void)user_data;
	(void)room;
	printf("Track stream state changed: %s from %s, state=%d\n", track->sid, participant->identity,
	       (int)state);
}

static void on_track_subscription_status_changed(void* user_data, lk_room_t* room,
                                                 const lk_track_publication_info_t* track,
                                                 const lk_participant_info_t* participant,
                                                 lk_track_subscription_status_t status) {
	(void)user_data;
	(void)room;
	printf("Track subscription status changed: %s from %s, status=%d\n", track->sid,
	       participant->identity, (int)status);
}

static lk_rpc_handler_result_t on_echo_rpc(void* user_data, const lk_rpc_invocation_t* invocation) {
	lk_rpc_handler_result_t result = {0};
	(void)user_data;
	printf("RPC from %s: %s\n", invocation->caller_identity, invocation->payload);
	result.payload = invocation->payload;
	return result;
}

static void on_text_stream(void* user_data, lk_room_t* room, const lk_text_stream_event_t* event) {
	(void)user_data;
	(void)room;
	printf("Text stream %s: state=%d, chunk=%llu, bytes=%zu, reason=%s\n", event->stream_id,
	       (int)event->type, (unsigned long long)event->chunk_index, event->content_size,
	       event->reason);
}

static void on_data_channel_buffer_status(void* user_data, lk_room_t* room,
                                          const lk_data_channel_buffer_status_t* status) {
	(void)user_data;
	(void)room;
	printf("Data channel backpressure: reliable=%d, active=%d, buffered=%llu\n", status->reliable,
	       status->backpressured, (unsigned long long)status->buffered_amount);
}

static void on_sip_dtmf(void* user_data, lk_room_t* room, const lk_sip_dtmf_t* event) {
	(void)user_data;
	(void)room;
	printf("SIP DTMF received: code=%u, digit=%s, from=%s\n", event->code, event->digit,
	       event->participant_identity);
}

static void on_chat_message(void* user_data, lk_room_t* room, const lk_chat_message_t* event) {
	(void)user_data;
	(void)room;
	printf("Chat message received: id=%s, edited=%d, text=%s, from=%s\n", event->id,
	       event->has_edit_timestamp, event->message, event->participant_identity);
}

static void on_transcription(void* user_data, lk_room_t* room,
                             const lk_transcription_received_t* event) {
	(void)user_data;
	(void)room;
	printf("Transcription received: participant=%s, track=%s, segments=%zu\n",
	       event->transcribed_participant_identity, event->track_id, event->segment_count);
	for (size_t index = 0; index < event->segment_count; ++index) {
		const lk_transcription_segment_t* segment = &event->segments[index];
		printf("  id=%s, final=%d, language=%s, text=%s\n", segment->id, segment->is_final,
		       segment->language, segment->text);
	}
}

static void on_recording_status(void* user_data, lk_room_t* room, int recording) {
	(void)user_data;
	(void)room;
	printf("Recording status changed: %s\n", recording ? "active" : "inactive");
}

static void on_metrics(void* user_data, lk_room_t* room, const lk_metrics_received_t* event) {
	(void)user_data;
	(void)room;
	printf("Metrics received: series=%zu, events=%zu, from=%s\n", event->time_series_count,
	       event->event_count, event->participant_identity);
}

static void on_stream_complete(void* user_data, const lk_data_stream_completion_t* completion) {
	(void)user_data;
	printf("Data stream completed: id=%s, status=%d, bytes=%llu, reason=%s\n",
	       completion->stream_id, (int)completion->status,
	       (unsigned long long)completion->bytes_sent, completion->reason);
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

static void print_remote_participant_snapshot(const lk_room_t* room) {
	lk_remote_participant_list_t* snapshot = NULL;
	if (lk_room_create_remote_participant_snapshot(room, &snapshot) != LK_STATUS_OK) {
		fprintf(stderr, "Participant snapshot failed: %s\n", lk_last_error());
		return;
	}
	const size_t participant_count = lk_remote_participant_list_count(snapshot);
	printf("Remote participant snapshot: %zu participant(s)\n", participant_count);
	for (size_t participant_index = 0; participant_index < participant_count; ++participant_index) {
		const lk_remote_participant_snapshot_t* participant = NULL;
		if (lk_remote_participant_list_at(snapshot, participant_index, &participant) !=
		    LK_STATUS_OK) {
			continue;
		}
		const size_t identity_size = lk_remote_participant_snapshot_identity(participant, NULL, 0);
		char* identity = identity_size != 0 ? (char*)malloc(identity_size) : NULL;
		if (identity != NULL) {
			lk_remote_participant_snapshot_identity(participant, identity, identity_size);
		}
		const size_t publication_count =
		    lk_remote_participant_snapshot_publication_count(participant);
		printf("  %s: %zu publication(s)\n", identity != NULL ? identity : "<unknown>",
		       publication_count);
		free(identity);
		for (size_t publication_index = 0; publication_index < publication_count;
		     ++publication_index) {
			const lk_remote_track_publication_snapshot_t* publication = NULL;
			lk_remote_track_publication_snapshot_info_t info;
			if (lk_remote_participant_snapshot_publication_at(participant, publication_index,
			                                                  &publication) != LK_STATUS_OK) {
				continue;
			}
			lk_remote_track_publication_snapshot_info_init(&info);
			if (lk_remote_track_publication_snapshot_info(publication, &info) == LK_STATUS_OK) {
				printf("    kind=%d source=%d subscribed=%s\n", (int)info.kind, (int)info.source,
				       info.has_subscribed_track ? "yes" : "no");
			}
		}
	}
	lk_remote_participant_list_destroy(snapshot);
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
	callbacks.on_connection_state_changed = on_connection_state;
	callbacks.on_disconnected_with_reason = on_disconnected;
	callbacks.on_participant_connected = on_participant_connected;
	callbacks.on_participant_permissions_changed = on_participant_permissions;
	callbacks.on_track_published = on_track_published;
	callbacks.on_local_track_subscribed = on_local_track_subscribed;
	callbacks.on_subscribed_quality_update = on_subscribed_quality_update;
	callbacks.on_track_subscription_failed = on_track_subscription_failed;
	callbacks.on_track_unsubscribed = on_track_unsubscribed;
	callbacks.on_track_stream_state_changed = on_track_stream_state_changed;
	callbacks.on_track_subscription_status_changed = on_track_subscription_status_changed;
	callbacks.on_data_channel_buffer_status_changed = on_data_channel_buffer_status;
	callbacks.on_sip_dtmf_received = on_sip_dtmf;
	callbacks.on_chat_message_received = on_chat_message;
	callbacks.on_transcription_received = on_transcription;
	callbacks.on_recording_status_changed = on_recording_status;
	callbacks.on_metrics_received = on_metrics;
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
	if (lk_room_register_text_stream_handler(room, "c.stream-demo", on_text_stream, NULL) !=
	    LK_STATUS_OK) {
		fprintf(stderr, "Text stream registration failed: %s\n", lk_last_error());
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
	print_remote_participant_snapshot(room);
	{
		const char* digit = getenv("LIVEKIT_DTMF_DIGIT");
		const char* code = getenv("LIVEKIT_DTMF_CODE");
		if (digit != NULL && digit[0] != '\0' &&
		    lk_room_publish_dtmf(room, code != NULL ? (uint32_t)strtoul(code, NULL, 10) : 0,
		                         digit) != LK_STATUS_OK) {
			fprintf(stderr, "SIP DTMF publish failed: %s\n", lk_last_error());
		}
	}
	{
		const char* message = getenv("LIVEKIT_CHAT_MESSAGE");
		if (message != NULL) {
			char message_id[LK_CHAT_MESSAGE_ID_BUFFER_SIZE];
			int64_t timestamp = 0;
			if (lk_room_send_chat_message(room, message, message_id, sizeof(message_id),
			                              &timestamp) != LK_STATUS_OK) {
				fprintf(stderr, "Chat message send failed: %s\n", lk_last_error());
			} else {
				printf("Chat message sent: id=%s, timestamp=%lld\n", message_id,
				       (long long)timestamp);
			}
		}
	}
	{
		const char* first = "incremental ";
		const char* second = "text from C";
		lk_stream_text_options_t options;
		lk_text_stream_writer_t* writer = NULL;
		lk_stream_text_options_init(&options);
		options.topic = "c.stream-demo";
		options.compress = 1;
		options.on_complete = on_stream_complete;
		options.has_total_size = 1;
		options.total_size = strlen(first) + strlen(second);
		if (lk_room_stream_text(room, &options, &writer) != LK_STATUS_OK ||
		    lk_text_stream_writer_write(writer, first, strlen(first)) != LK_STATUS_OK ||
		    lk_text_stream_writer_write(writer, second, strlen(second)) != LK_STATUS_OK ||
		    lk_text_stream_writer_close(writer) != LK_STATUS_OK) {
			fprintf(stderr, "Incremental text stream failed: %s\n", lk_last_error());
		}
		lk_text_stream_writer_destroy(writer);
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
