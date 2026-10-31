lk_status_t TokenSource(void* user, const lk_token_source_fetch_options_t* options,
                        int force_refresh, lk_token_source_response_t* response) noexcept {
	try {
		auto& state = *static_cast<Room*>(user);
		if (!state.channel)
			return LK_STATUS_INVALID_STATE;
		auto reply = state.channel->Ask(
		    "token", Object{{"options", Pack(*options)}, {"forceRefresh", bool(force_refresh)}});
		if (!VBool(reply, "ok"))
			return LK_STATUS_OPERATION_FAILED;
		thread_local std::string url, token;
		url = VString(reply, "serverUrl");
		token = VString(reply, "participantToken");
		if (url.empty() || token.empty())
			return LK_STATUS_INVALID_ARGUMENT;
		response->server_url = url.c_str();
		response->participant_token = token.c_str();
		return LK_STATUS_OK;
	} catch (...) {
		return LK_STATUS_OPERATION_FAILED;
	}
}
int ReconnectPolicy(void* user, const lk_reconnect_context_t* context, uint32_t* delay) noexcept {
	try {
		auto& state = *static_cast<Room*>(user);
		if (!state.channel)
			return 0;
		const auto reply = state.channel->Ask("reconnect", Pack(*context));
		if (!VBool(reply, "ok") || !VBool(reply, "retry"))
			return 0;
		*delay = VNumber(reply, "delayMs");
		return 1;
	} catch (...) {
		return 0;
	}
}
void RpcHandler(void* user, const lk_rpc_invocation_t* invocation,
                lk_rpc_handler_result_t* result) noexcept {
	thread_local std::string payload, message, data;
	*result = {};
	try {
		auto& context = *static_cast<RpcContext*>(user);
		if (!context.room->channel)
			throw Error("Callback channel unavailable");
		const auto reply = context.room->channel->Ask(
		    "rpc", Object{{"method", context.method}, {"invocation", Pack(*invocation)}},
		    invocation->response_timeout_ms);
		payload = VString(reply, "payload");
		if (VBool(reply, "ok")) {
			result->payload = payload.c_str();
			return;
		}
		message = VString(reply, "message", "RPC handler failed");
		data = VString(reply, "data");
		result->error_code = VNumber(reply, "code", LK_RPC_ERROR_APPLICATION_ERROR);
		if (!result->error_code)
			result->error_code = LK_RPC_ERROR_APPLICATION_ERROR;
	} catch (const std::exception& e) {
		message = e.what();
		data.clear();
		result->error_code = LK_RPC_ERROR_APPLICATION_ERROR;
	} catch (...) {
		message = "RPC handler failed";
		data.clear();
		result->error_code = LK_RPC_ERROR_APPLICATION_ERROR;
	}
	result->error_message = message.c_str();
	result->error_data = data.c_str();
}
void TextStreamHandler(void* user, lk_room_t*, const lk_text_stream_event_t* e) noexcept {
	Callback(user, [&](Room& r) { r.Push(Object{{"type", "textStream"}, {"event", Pack(*e)}}); });
}
void ByteStreamHandler(void* user, lk_room_t*, const lk_byte_stream_event_t* e) noexcept {
	Callback(user, [&](Room& r) { r.Push(Object{{"type", "byteStream"}, {"event", Pack(*e)}}); });
}
void WriterProgress(void* user, uint64_t sent, int total, uint64_t size) noexcept {
	try {
		auto& c = *static_cast<WriterContext*>(user);
		c.room->Push(Object{{"type", "streamProgress"},
		                    {"id", c.id},
		                    {"bytesSent", sent},
		                    {"hasTotalSize", bool(total)},
		                    {"totalSize", size}});
	} catch (...) {
	}
}
void WriterComplete(void* user, const lk_data_stream_completion_t* completion) noexcept {
	try {
		auto& c = *static_cast<WriterContext*>(user);
		c.room->Push(
		    Object{{"type", "streamComplete"}, {"id", c.id}, {"completion", Pack(*completion)}});
	} catch (...) {
	}
}
void AdditionalCallbacks(lk_room_callbacks_t& c) {
#define SIMPLE_EVENT(field, name, CType)                                                           \
	c.field = [](void* u, lk_room_t*, const CType* e) {                                            \
		Callback(u, [&](Room& r) { r.Push(Object{{"type", name}, {"event", Pack(*e)}}); });        \
	};
	SIMPLE_EVENT(on_sip_dtmf_received, "sipDtmfReceived", lk_sip_dtmf_t)
	SIMPLE_EVENT(on_chat_message_received, "chatMessageReceived", lk_chat_message_t)
	SIMPLE_EVENT(on_transcription_received, "transcriptionReceived", lk_transcription_received_t)
	SIMPLE_EVENT(on_metrics_received, "metricsReceived", lk_metrics_received_t)
	SIMPLE_EVENT(on_data_channel_buffer_status_changed, "dataChannelBufferStatusChanged",
	             lk_data_channel_buffer_status_t)
	SIMPLE_EVENT(on_encryption_state_changed, "encryptionStateChanged", lk_encryption_state_t)
	SIMPLE_EVENT(on_room_updated, "roomUpdated", lk_room_snapshot_t)
	SIMPLE_EVENT(on_room_moved, "roomMoved", lk_room_snapshot_t)
#undef SIMPLE_EVENT
	c.on_room_sid_changed = [](void* u, lk_room_t*, const char* previous, const char* sid) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "roomSidChanged"}, {"previousSid", previous}, {"sid", sid}});
		});
	};
	c.on_recording_status_changed = [](void* u, lk_room_t*, int enabled) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "recordingStatusChanged"}, {"recording", bool(enabled)}});
		});
	};
	c.on_connection_quality_changed = [](void* u, lk_room_t*, lk_connection_quality_t quality,
	                                     const lk_participant_info_t* p) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "connectionQualityChanged"},
			              {"quality", int(quality)},
			              {"participant", Participant(p)}});
		});
	};
	c.on_active_speakers_changed = [](void* u, lk_room_t*, const lk_participant_info_t* p,
	                                  size_t count) {
		Callback(u, [&](Room& r) {
			Array items;
			for (size_t i = 0; i < count; ++i)
				items.push_back(Participant(p + i));
			r.Push(Object{{"type", "activeSpeakersChanged"}, {"participants", std::move(items)}});
		});
	};
	c.on_participants_updated = [](void* u, lk_room_t*, const lk_participant_info_t* p,
	                               size_t count) {
		Callback(u, [&](Room& r) {
			Array items;
			for (size_t i = 0; i < count; ++i)
				items.push_back(Participant(p + i));
			r.Push(Object{{"type", "participantsUpdated"}, {"participants", std::move(items)}});
		});
	};
	c.on_participant_metadata_changed = [](void* u, lk_room_t*, const char* previous,
	                                       const lk_participant_info_t* p) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "participantMetadataChanged"},
			              {"previousMetadata", previous},
			              {"participant", Participant(p)}});
		});
	};
	c.on_participant_name_changed = [](void* u, lk_room_t*, const char* name,
	                                   const lk_participant_info_t* p) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "participantNameChanged"},
			              {"name", name},
			              {"participant", Participant(p)}});
		});
	};
	c.on_participant_attributes_changed = [](void* u, lk_room_t*, const lk_attribute_t* attributes,
	                                         size_t count, const lk_participant_info_t* p) {
		Callback(u, [&](Room& r) {
			Array values;
			for (size_t i = 0; i < count; ++i)
				values.push_back(Pack(attributes[i]));
			r.Push(Object{{"type", "participantAttributesChanged"},
			              {"changes", std::move(values)},
			              {"participant", Participant(p)}});
		});
	};
	c.on_participant_permissions_changed =
	    [](void* u, lk_room_t*, const lk_participant_permissions_t* previous,
	       const lk_participant_permissions_t* permissions, const lk_participant_info_t* p) {
		    Callback(u, [&](Room& r) {
			    r.Push(Object{{"type", "participantPermissionsChanged"},
			                  {"previousPermissions", previous ? Pack(*previous) : Value()},
			                  {"permissions", permissions ? Pack(*permissions) : Value()},
			                  {"participant", Participant(p)}});
		    });
	    };
#define TRACK_STATE_EVENT(field, name, CType, key)                                                 \
	c.field = [](void* u, lk_room_t*, const lk_track_publication_info_t* t,                        \
	             const lk_participant_info_t* p, CType state) {                                    \
		Callback(u, [&](Room& r) {                                                                 \
			r.Push(Object{{"type", name},                                                          \
			              {"track", Track(t)},                                                     \
			              {"participant", Participant(p)},                                         \
			              {key, int(state)}});                                                     \
		});                                                                                        \
	};
	TRACK_STATE_EVENT(on_track_subscription_permission_changed,
	                  "trackSubscriptionPermissionChanged", int, "allowed")
	TRACK_STATE_EVENT(on_track_subscription_failed, "trackSubscriptionFailed",
	                  lk_subscription_error_t, "error")
	TRACK_STATE_EVENT(on_track_stream_state_changed, "trackStreamStateChanged",
	                  lk_track_stream_state_t, "state")
	TRACK_STATE_EVENT(on_track_subscription_status_changed, "trackSubscriptionStatusChanged",
	                  lk_track_subscription_status_t, "status")
#undef TRACK_STATE_EVENT
	c.on_subscribed_quality_update = [](void* u, lk_room_t*, const lk_track_publication_info_t* t,
	                                    const lk_participant_info_t* p,
	                                    const lk_subscribed_quality_update_t* update) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "subscribedQualityUpdate"},
			              {"track", Track(t)},
			              {"participant", Participant(p)},
			              {"update", Pack(*update)}});
		});
	};
	c.on_local_track_subscribed = [](void* u, lk_room_t*, const lk_track_publication_info_t* t,
	                                 const lk_participant_info_t* p) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "localTrackSubscribed"},
			              {"track", Track(t)},
			              {"participant", Participant(p)}});
		});
	};
#define DATA_TRACK_EVENT(field, name)                                                              \
	c.field = [](void* u, lk_room_t*, const lk_data_track_info_t* t,                               \
	             const lk_participant_info_t* p) {                                                 \
		Callback(u, [&](Room& r) {                                                                 \
			r.Push(Object{{"type", name}, {"track", Pack(*t)}, {"participant", Participant(p)}});  \
		});                                                                                        \
	};
	DATA_TRACK_EVENT(on_data_track_published, "dataTrackPublished")
	DATA_TRACK_EVENT(on_data_track_unpublished, "dataTrackUnpublished")
	DATA_TRACK_EVENT(on_local_data_track_published, "localDataTrackPublished")
	DATA_TRACK_EVENT(on_local_data_track_unpublished, "localDataTrackUnpublished")
#undef DATA_TRACK_EVENT
	c.on_data_track_frame = [](void* u, lk_room_t*, const lk_data_track_info_t* t,
	                           const lk_participant_info_t* p,
	                           const lk_data_track_frame_view_t* f) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "dataTrackFrame"},
			              {"track", Pack(*t)},
			              {"participant", Participant(p)},
			              {"frame", Pack(*f)}});
		});
	};
	c.on_video_frame_with_metadata = [](void* u, lk_room_t*, const lk_track_publication_info_t* t,
	                                    const lk_participant_info_t* p, const lk_video_frame_t* f,
	                                    const lk_video_frame_metadata_t* m) {
		Callback(u, [&](Room& r) {
			r.Push(Object{{"type", "videoFrameWithMetadata"},
			              {"track", Track(t)},
			              {"participant", Participant(p)},
			              {"frame", Pack(*f)},
			              {"metadata", m ? Pack(*m) : Value()}});
		});
	};
}

std::mutex diagnostics_config_mutex;
std::mutex diagnostics_sinks_mutex;
std::map<Room*, std::pair<bool, bool>> diagnostics_sinks;
bool log_installed = false, trace_installed = false;
Room* trace_file_owner = nullptr;
void LogSink(void*, const lk_log_record_t* record) noexcept {
	try {
		std::lock_guard lock(diagnostics_sinks_mutex);
		for (const auto& [room, flags] : diagnostics_sinks)
			if (flags.first)
				room->Push(Object{{"type", "log"}, {"record", Pack(*record)}});
	} catch (...) {
	}
}
void TraceSink(void*, const lk_trace_record_t* record) noexcept {
	try {
		std::lock_guard lock(diagnostics_sinks_mutex);
		for (const auto& [room, flags] : diagnostics_sinks)
			if (flags.second)
				room->Push(Object{{"type", "trace"}, {"record", Pack(*record)}});
	} catch (...) {
	}
}
void UpdateDiagnosticsLocked() {
	bool logs = false, traces = false;
	{
		std::lock_guard lock(diagnostics_sinks_mutex);
		for (const auto& [_, flags] : diagnostics_sinks) {
			logs |= flags.first;
			traces |= flags.second;
		}
	}
	if (logs != log_installed) {
		Check(lk_log_set_callback(logs ? LogSink : nullptr, nullptr));
		log_installed = logs;
	}
	if (!trace_file_owner && traces != trace_installed) {
		Check(lk_trace_set_callback(traces ? TraceSink : nullptr, nullptr));
		trace_installed = traces;
	}
}
void RemoveDiagnostics(Room* room) {
	std::lock_guard config_lock(diagnostics_config_mutex);
	{
		std::lock_guard lock(diagnostics_sinks_mutex);
		diagnostics_sinks.erase(room);
	}
	try {
		if (trace_file_owner == room) {
			lk_trace_stop();
			trace_file_owner = nullptr;
		}
		UpdateDiagnosticsLocked();
	} catch (...) {
	}
}
